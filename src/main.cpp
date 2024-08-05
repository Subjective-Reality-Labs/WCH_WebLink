#include <Arduino.h>
#include <stdio.h>
// #include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#ifdef ARDUINO_OTA
#include "OTA.h"
#endif
#include <LittleFS.h>
#include <FS.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <PersWiFiManagerAsync.h>
#include "SRLConfig.h"
#include "LittleFS_helpers.h"
#include "ch32v003_swio.h"
#include "driver/gpio.h"

#define DEVICE_NAME "WebLink"
#define MDNS_NAME "weblink"

#define WCH_MAX_TIMEOUT 30
#define TERMINAL_BUFFER_SIZE 1024
#define MAX_BINARY_SIZE 16384
#define DEFAULT_FLASH_OFFSET 0x08000000
#define FLASHER_OP_TIMEOUT 10000

#define HALT_MODE_HALT_AND_RESET    0
#define HALT_MODE_REBOOT            1
#define HALT_MODE_RESUME            2
#define HALT_MODE_GO_TO_BOOTLOADER  3
#define HALT_MODE_HALT_BUT_NO_RESET 5

typedef enum WLFlasherStatus {
  WLF_IDLE,
  WLF_UPLOADING,
  WLF_UPDATING,
  WLF_SUCCESS,
  WLF_FAILED,
} WLFlasherStatus_t;

typedef enum WLFlasherError {
  WLF_UNKNOWN_ERROR,
  WLF_UPLOAD_ERROR,
  WLF_VERIFY_ERROR,
  WLF_READ_ERROR,
  WLF_UNPACK_ERROR,
  WLF_UPDATER_ERROR,
  WLF_TIMEOUT,
} WLFlasherError_t;

typedef enum WLFlasherCommand {
  WLF_NONE,
  WLF_INFO,
  WLF_FLASH,
  WLF_READ,
  WLF_RESET,
  WLF_HALT,
  WLF_ERASE,
  WLF_UNBRICK,
  WLF_DEBUG,
} WLFlasherCommand_t;

ConfigG config;
AsyncWebServer server(80);
AsyncWebSocket terminal_ws("/terminal");
AsyncWebSocket flash_ws("/wsflash");
AsyncEventSource link_events = AsyncEventSource("/events");
DNSServer dns_server;
PersWiFiManagerAsync persWM(server, dns_server);

const char *config_file = "/config.json";

String device_id;
bool AP_active = false;

struct Terminal {
  bool connected = false;
  char buf[TERMINAL_BUFFER_SIZE];
  char incomming_buf[64];
  uint8_t incomming_pos = 0;
  uint32_t last_send_time = 0;
} terminal;

struct Flasher {
  bool active = false;
  bool will_flash = false;
  bool will_unbrick = false;
  bool will_read = false;
  uint32_t offset = 0;
  uint32_t size = 0;
  char message[64];
  char name [64];
  WLFlasherError_t error;
  WLFlasherStatus_t status;
  uint8_t retries = 0;
  uint8_t current_retry = 0;
  uint32_t watchdog = 0;
} flasher;

struct FlasherWS {
  bool active = false;
  WLFlasherCommand_t current_command = WLF_NONE;
  AsyncWebSocketClient* client;
  uint32_t watchdog = 0;
} flasher_ws;

TaskHandle_t PollTask;

struct SWIOState link_state;
uint8_t binary_buf[16384];
bool upload_post_error;

int initLink();
int writeBinary(uint32_t offset, uint32_t size);
int unbrick();
int chipInfo(char* buf);
void pollTerminal(void *pvParameter);
void handleFlasher();
void parseMessage(char* message);
void uartSetup();
void terminalDisconnect();


////////////////////////////////
///   Server functions       ///
////////////////////////////////
void onRequest(AsyncWebServerRequest *request)
{
  // Handle Unknown Request
  if (request->method() == HTTP_OPTIONS)
  {
    request->send(200);
  }
  else
  {
    request->send(404);
  }
}

void onBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  // Handle body
}

void onUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  // Handle upload
}

void onFlashRequest(AsyncWebServerRequest *request) {
  // the request handler is triggered after the upload has finished... 
  // create the response, add header, and send response
  AsyncWebServerResponse *response = request->beginResponse((upload_post_error)?500:200, "text/plain", (upload_post_error)?"FAIL":"OK");
  response->addHeader("Connection", "close");
  request->send(response);
  if (!upload_post_error) {
    Serial.println("Upload ok");
  } else {
    Serial.println("Upload failed");
    upload_post_error = false;
  }
};

void resetFlasher() {
  flasher.active = false;
  flasher_ws.active = false;
  flasher.will_flash = false;
  flasher.will_unbrick = false;
  flasher.will_read = false;
  flasher.offset = 0;
  flasher.size = 0;
  flasher.retries = 0;
  flasher.current_retry = 0;
  flasher_ws.current_command = WLF_NONE;
}

void activateFlasher(bool ws = false) {
  flasher.active = true;
  flasher_ws.active = ws;
  flasher.watchdog = millis();
}

int resetCH() {
  Serial.println("Resetting the board");
  if(initLink() < 1) {
    return 1;
  } else {
    HaltMode(&link_state, 1);
    delay(10);
    return 0;
  }
}

void onFlashUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    
    if (!index) {
      // if (flasher.status == WLF_UPDATING || flasher.status == WLF_UPLOADING) {
      if (millis() - flasher.watchdog < FLASHER_OP_TIMEOUT) resetFlasher();
      if (flasher.active) {
        upload_post_error = true;
        Serial.println("Flash in progress");
        return request->send(400, "text/plain", "Flash in progress");
      } else {
        flasher.status = WLF_UPLOADING;
        upload_post_error = false;
      }
      if (!request->hasParam("size", true)) {
        upload_post_error = true;
        flasher.error = WLF_UPLOAD_ERROR;
        Serial.println("Size parameter missing.");
        return request->send(400, "text/plain", "Size parameter missing.");        
      }
      int binary_size = request->getParam("size", true)->value().toInt();
      if (!request->hasParam("offset", true)) {
        upload_post_error = true;
        flasher.error = WLF_UPLOAD_ERROR;
        Serial.print("Offset not specified");
        return request->send(400, "text/plain", "Offset parameter missing.");
      } else {
        flasher.offset = request->getParam("offset", true)->value().toInt();
      }
      if (MAX_BINARY_SIZE < binary_size) {
        upload_post_error = true;
        flasher.error = WLF_UPLOAD_ERROR;
        Serial.println("Binary is bigger then ch32v003 flash size.");
        return request->send(400, "text/plain", "Binary is too big.");
      } else {
        flasher.size = binary_size;
      }
      flasher.active = true;
      Serial.printf("Starting binary upload. size = %d\n\r", binary_size);
    }
    if(len){
      memcpy(binary_buf+index, data, len);
      sprintf(flasher.message, "%d/%" PRIu32 "", (int)(index+len), flasher.size);
      Serial.println(flasher.message);
      link_events.send(flasher.message, "flasher", millis());
    }
    
    if (final) { // if the final flag is set then this is the last frame of data
      if (index+len != flasher.size) {
        upload_post_error = true;
        flasher.error = WLF_UPLOAD_ERROR;
        flasher.status = WLF_FAILED;
        resetFlasher();
        sprintf(flasher.message, "Size mismatch, expected:%" PRIu32 ", actual:%d", flasher.size, (int)(index+len));
        Serial.println(flasher.message);
        link_events.send(flasher.message, "flasher", millis());
        return request->send(400, "text/plain", "Size mismatch");
      } else {
        upload_post_error = false;
        flasher.will_flash = true;
        flasher.status = WLF_UPDATING;
        flasher.current_retry = 0;
        if (request->hasParam("retries", true)) {
          flasher.retries = request->getParam("retries", true)->value().toInt();
        } else {
          flasher.retries = 0;
        }
        Serial.println("Will flash");
        link_events.send("Will flash", "flasher", millis());
      }
      return;
    }
}

void onStatus(AsyncWebServerRequest *request) {
  char reply[64];
  if (flasher.status == WLF_IDLE) {
    request->send(200, "text/plain", "Idle");
  } else if (flasher.status == WLF_UPLOADING) {
    request->send(200, "text/plain", "Uploading binary");
  } else if (flasher.status == WLF_UPDATING) {
    sprintf(reply, "Flashing in progress. Retry: %u", flasher.current_retry);
    // %" PRIu32 "
    request->send(200, "text/plain", reply);
  } else if (flasher.status == WLF_FAILED) {
    request->send(200, "text/plain", "Flashing failed");
  } else if (flasher.status == WLF_SUCCESS) {
    request->send(200, "text/plain", "Successfully flashed");
  }
}

void onTerminalEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  // Handle WebSocket event
  switch (type) {
  case WS_EVT_CONNECT:
    // client connected
    Serial.printf("ws[%s][%u] connect\n\r", server->url(), client->id());
    client->printf("Hello Client %u" PRIu32 " :)", client->id());
    if (config.uart == false){
      if (initLink() > 0) {
        terminal.connected = true;
      } else {
        terminal.connected = false;
        client->printf("Failed to connect to ch32v003");
        client->close();
      }
    } else {
      terminal.connected = true;
      Serial.println("Using UART for terminal");
    }
    break;
  case WS_EVT_DISCONNECT:
    // client disconnected
    Serial.printf("ws[%s][%" PRIu32 "] disconnect\n\r", server->url(), client->id());
    terminalDisconnect();
    break;
  case WS_EVT_ERROR:
    // error was received from the other end
    Serial.printf("ws[%s][%" PRIu32 "] error(%u): %s\n\r", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
    break;
  case WS_EVT_PONG:
    // pong message was received (in response to a ping request maybe)
    Serial.printf("ws[%s][%" PRIu32 "] pong[%u]: %s\n\r", server->url(), client->id(), len, (len) ? (char *)data : "");
    break;
  case WS_EVT_DATA:
    // data packet
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len) {
      // the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%" PRIu32 "] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);
      if (info->opcode == WS_TEXT && data[0] == 35) {
        Serial.printf("%s. Sendind to terminal\n\r", (char *)data);
        if (terminal.incomming_buf[terminal.incomming_pos] == 0) {
          strncpy(terminal.incomming_buf, (char *)data+1, min(int(len-1), (int)sizeof(terminal.incomming_buf)));
        }
      } else  if (info->opcode == WS_TEXT) {
        Serial.printf("%s\n\r", (char *)data);
      } else {
        for (size_t i = 0; i < info->len; i++) {
          Serial.printf("%02x ", data[i]);
        }
        Serial.print("\n\r");
      }
    }
    break;
  }
}

void onFlasherEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  // Handle WebSocket event
  switch (type) {
  case WS_EVT_CONNECT:
    // client connected
    Serial.printf("ws[%s][%u] flasher connect\n\r", server->url(), client->id());
    client->printf("Welcome to WebLink flasher. Your id is %u" PRIu32 " :)", client->id());
    break;
  case WS_EVT_DISCONNECT:
    // client disconnected
    Serial.printf("ws[%s][%" PRIu32 "] flasher disconnect\n\r", server->url(), client->id());
    break;
  case WS_EVT_ERROR:
    // error was received from the other end
    Serial.printf("ws[%s][%" PRIu32 "] error(%u): %s\n\r", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
    break;
  case WS_EVT_PONG:
    // pong message was received (in response to a ping request maybe)
    Serial.printf("ws[%s][%" PRIu32 "] pong[%u]: %s\n\r", server->url(), client->id(), len, (len) ? (char *)data : "");
    break;
  case WS_EVT_DATA:
    // data packet
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len) {
      // the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%" PRIu32 "] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);
      if (info->opcode == WS_TEXT) {
        Serial.printf("%s\n\r", (char *)data);
        char buffer[64];
        strncpy(buffer, (char *)data, 64);
        // Serial.println(buffer);
        if (buffer[0] == '#') {
          Serial.printf("[ws] Got a command: %s\n\r", buffer);
          if (flasher.active || flasher_ws.active) {
            client->printf("#1;Flasher busy");
            break;
          }
          if (initLink() < 1) {
            client->text("#2;Failed to init link");
            break;
          };
          flasher_ws.client = client;
          activateFlasher(true);
          switch (buffer[1])
          {
          case '3':
          case '5':
          case 't':
          case 'f':
          case 'U':
            client->printf("#8;Unimplemented");
            resetFlasher();
            break;
          case 'b': //reBoot
            flasher_ws.current_command = WLF_RESET;
            HaltMode(&link_state, HALT_MODE_REBOOT);
            resetFlasher();
            break;
          case 'B': //reBoot into Bootloader
            flasher_ws.current_command = WLF_RESET;
            HaltMode(&link_state, HALT_MODE_GO_TO_BOOTLOADER);
            client->text("#0;Reboted to bootloader");
            resetFlasher();
            break;
          case 'e': //rEsume
            flasher_ws.current_command = WLF_RESET;
            HaltMode(&link_state, HALT_MODE_RESUME);
            client->text("#0;Resumed");
            resetFlasher();
            break;
          case 'a': //Reboot into Halt
            flasher_ws.current_command = WLF_HALT;
            HaltMode(&link_state, HALT_MODE_HALT_AND_RESET);
            client->text("#0;Reboted to halt");
            resetFlasher();
            break;
          case 'A': // Halt without reboot
            flasher_ws.current_command = WLF_HALT;
            HaltMode(&link_state, HALT_MODE_HALT_BUT_NO_RESET);
            client->text("#0;Halted");
            resetFlasher();
            break;
          case 'd': // disable NRST pin (turn it into a GPIO)
            // HaltMode(&link_state, HALT_MODE_HALT_AND_RESET);
            // ConfigureNRSTAsGPIO(&link_state, 0);
            // client->text("#0;NRST disabled");
            // resetFlasher();
            // break;
          case 'D':
            // HaltMode(&link_state, HALT_MODE_HALT_AND_RESET);
            // ConfigureNRSTAsGPIO(&link_state, 1);
            // client->text("#0;NRST enabled");
            // resetFlasher();
            // break;
          case 'p':
            // HaltMode(&link_state, HALT_MODE_HALT_AND_RESET);
            // ConfigureReadProtection(&link_state, 0);
            // client->text("#0;Read protection off");
            // resetFlasher();
            // break;
          case 'P':
            // HaltMode(&link_state, HALT_MODE_HALT_AND_RESET);
            // client->text("#0;Read protection on");
            // ConfigureReadProtection(&link_state, 1);
            client->text("#8;Unimplemented");
            resetFlasher();
          case 's':
            char* token;
            uint32_t datareg, value;
            flasher_ws.current_command = WLF_DEBUG;
            token = strtok(buffer, ";");
            token = strtok(NULL, ";");
            if (token == NULL) {
              client->text("#3;Register missing");
              resetFlasher();
              break;
            }
            datareg = atoi(token);
            token = strtok(NULL, ";");
            if (token == NULL) {
              client->text("#3;Value missing");
              resetFlasher();
              break;
            }
            value = atoi(token);
            MCFWriteReg32(&link_state, datareg, value);
            client->text("#0;Register written");
            resetFlasher();
            break;
          case 'm':
            char* token;
            uint32_t datareg, value;
            flasher_ws.current_command = WLF_DEBUG;
            token = strtok(buffer, ";");
            token = strtok(NULL, ";");
            if (token == NULL) {
              client->text("#3;Register missing");
              resetFlasher();
              break;
            }
            datareg = atoi(token);
            int ret = MCFReadReg32(&link_state, datareg, &value);
            client->printf("#0;%" PRIu32 ";%" PRIu32 ";%d", datareg, value, ret);
            resetFlasher();
            break;
          case 'w':
            char* token;
            token = strtok(buffer, ";");
            token = strtok(NULL, ";");
            if (token == NULL) {
              client->printf("#3;Offset missing");
              resetFlasher();
              break;
            }
            flasher.offset = atoi(token);
            token = strtok(NULL, ";");
            if (token == NULL) {
              client->printf("#3;Size missing");
              resetFlasher();
              break;
            }
            flasher.size = atoi(token);
            if (flasher.size > MAX_BINARY_SIZE) {
              client->printf("#3;Binary is too big");
              resetFlasher();
              break;
            }
            token = strtok(NULL, ";");
            if (token != NULL) {
              flasher.retries = atoi(token);
              token = strtok(NULL, ";");
              if (token != NULL) strncpy(flasher.name, token, 64);
            }
            flasher.status = WLF_UPLOADING;
            flasher_ws.current_command = WLF_FLASH;
            client->printf("#0;Ready for upload");
            break;
          case 'r':
            flasher_ws.current_command = WLF_READ;
            client->printf("#8;Unimplemented");

            char* token;
            token = strtok(buffer, ";");
            token = strtok(NULL, ";");
            if (token == NULL) {
              client->printf("#3;Offset missing");
              resetFlasher();
              break;
            }
            flasher.offset = atoi(token);
            token = strtok(NULL, ";");
            if (token == NULL) {
              client->printf("#3;Size missing");
              resetFlasher();
              break;
            }
            flasher.size = atoi(token);
            if(flasher.offset > 0xffffffff || flasher.size > 0xffffffff ) {
            // if (flasher.size > MAX_BINARY_SIZE) {
              client->printf("#3;Memory value request out of range");
              resetFlasher();
              break;
            }
            flasher.status = WLF_UPLOADING;
            flasher_ws.current_command = WLF_READ;
            client->printf("#0;Ready for download");
            flasher.will_read = true;
            break;
          case 'u':
            flasher_ws.current_command = WLF_UNBRICK;
          case 'E':
            flasher_ws.current_command = WLF_ERASE;
            flasher.will_unbrick = true;
            break;
          case 'i':
            flasher_ws.current_command = WLF_INFO;
            char buffer[74];
            if (chipInfo(buffer)) {
              Serial.println("Failed to read info");
              client->printf("#4;Failed to read info");
            } else {
              Serial.println(buffer);
              client->text(buffer);
            }
            resetFlasher();
            break;

          default:
            resetFlasher();
            client->printf("#9;Unknown command");
            break;
          }
        }
      } else  if (info->opcode == WS_BINARY && flasher_ws.active && flasher.status == WLF_UPLOADING && client == flasher_ws.client) {
        Serial.println("Got binary in one message");
        flasher.watchdog = millis();
        if (len == flasher.size) {
          memcpy(binary_buf, data, len);
          printf(flasher.message, "%d/%" PRIu32 "", (int)(len), flasher.size);
          Serial.println(flasher.message);
          flasher.will_flash = true;
          flasher.status == WLF_UPDATING;
          client->printf("#0;Will flash");
        } else {
          resetFlasher();
          flasher.error = WLF_UPLOAD_ERROR;
          flasher.status = WLF_FAILED;
          client->printf("#4;Binary size mismatch");
        }
      }
    } else if (info->opcode == WS_BINARY && flasher_ws.active && flasher.status == WLF_UPLOADING && client == flasher_ws.client) {
      Serial.print("Got partial binary ");
      Serial.printf("index=%llu; len=%u; \n\r", info->index, len);
      flasher.watchdog = millis();
      memcpy(binary_buf+info->index, data, len);
      sprintf(flasher.message, "%" PRIu64 "/%" PRIu32 "", (info->index+len), info->len);
      Serial.println(flasher.message);
      if (info->index + len == info->len) {
        Serial.println("Final");
        if (info->len == flasher.size) {
          flasher.will_flash = true;
          flasher.status == WLF_UPDATING;
          client->printf("#0;Will flash");
        } else {
          resetFlasher();
          flasher.error = WLF_UPLOAD_ERROR;
          flasher.status = WLF_FAILED;
          client->printf("#4;Binary size mismatch");
        }
      }
    } else {
      Serial.println("Got something");
    }
    break;
  }
}

String wifiTemplate(const String& var)
{
  if(var == "WIFI") {
    if ((WiFi.getMode() == WIFI_AP)) {
      return F("AP"); 
    } else {
      return F("STA");
    }
  }
  return String();
}

String wifiTemplateForce(const String& var)
{
  if(var == "WIFI") {
    return F("AP");
  }
  return String();
}

AsyncCallbackJsonWebHandler settings_handler = AsyncCallbackJsonWebHandler("/rest/settings", [](AsyncWebServerRequest *request, JsonVariant &json) {
  request->send(200);
  JsonObject jsonObj = json.as<JsonObject>();
  config.fromJson(jsonObj);
  config.save(LittleFS, config_file);
  });

void webServerSetup() {
  // attach AsyncWebSocket
  terminal_ws.onEvent(onTerminalEvent);
  flash_ws.onEvent(onFlasherEvent);
  server.addHandler(&terminal_ws);
  server.addHandler(&flash_ws);
  server.addHandler(&link_events);
  server.addHandler(&settings_handler);

  server.on("/rest/settings", HTTP_GET, [](AsyncWebServerRequest *request) { 
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    config.serialize(*response);
    request->send(response); });

  #ifdef EXTERNAL_WEBUI //CORS workaround
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.on("/rest/settings", HTTP_OPTIONS, [](AsyncWebServerRequest *request) { 
    AsyncWebServerResponse *response = request->beginResponse(200);
    response->addHeader("Access-Control-Allow-Methods", "POST");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    request->send(response); });
  #endif

  // respond to GET requests on URL /heap
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) { 
    request->send(200, "text/plain", String(ESP.getFreeHeap())); });
  
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) { 
    if(resetCH()) {
      request->send(200, "text/plain", "Failed to init");
    } else {
      request->send(200, "text/plain", "OK");
    }
  });
  
  server.on("/unbrick", HTTP_GET, [](AsyncWebServerRequest *request) { 
    flasher.active = true;
    flasher.watchdog = millis();
    flasher.will_unbrick = true;
    if (config.pin3v3 < 0) {
      request->send(200, "text/plain", "Will erase");
    } else {
      request->send(200, "text/plain", "Will unbrick");
    }
  });

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) { 
    request->send(200); }, onUpload);

  link_events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got was: %u\n\r", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 1000);
  });

  server.on("/flash", HTTP_POST, onFlashRequest, onFlashUpload);

  server.on("/status", HTTP_GET, onStatus);

  server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request) { 
    // request->send(LittleFS, "/www/index.html");
    request->send(LittleFS, "/www/index.html", String(), false, wifiTemplate);
    });
  // send a file when /index is requested
  server.on("/index", HTTP_ANY, [](AsyncWebServerRequest *request) { 
    request->send(LittleFS, "/www/index.html", String(), false, wifiTemplate);
    });

  server.on("/wifi", HTTP_ANY, [](AsyncWebServerRequest *request) { 
    request->send(LittleFS, "/www/index.html", String(), false, wifiTemplateForce);
  });
  
  server.on("/hotspot-detect.html", HTTP_ANY, [](AsyncWebServerRequest *request) { 
    request->send(LittleFS, "/www/index.html", String(), false, wifiTemplateForce);
  });

  server.on("/canonical.html", HTTP_ANY, [](AsyncWebServerRequest *request) { 
    request->send(LittleFS, "/www/index.html", String(), false, wifiTemplateForce);
  });

  server.on("/success.txt", HTTP_ANY, [](AsyncWebServerRequest *request) { 
    request->send(404);
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) { 
    request->send(200, "text/plain", "OK");
    // persWM.resetSettings();
    ESP.restart();
  });

  server.serveStatic("/", LittleFS, "/www/");

  // Catch-All Handlers
  // Any request that can not find a Handler that canHandle it
  // ends in the callbacks below.
  server.onNotFound(onRequest);
  server.onFileUpload(onUpload);
  server.onRequestBody(onBody);
  server.begin();
}

////////////////////////////////
///   End of Server stuff    ///
////////////////////////////////  
///   Config callbacks       ///
////////////////////////////////
ConfigG::changeCallbacks configCallbacks = {
  // void (*uart_cb)(void);
  [](void) {
    terminalDisconnect();
    uartSetup();
    return;
  },
  // void (*swio_pin_cb)(void);
  [](void) {
    if (config.uart == false) {
      terminalDisconnect();
    }
    return;
  },
  // void (*t1coeff_cb)(void);
  [](void) {
    link_state.t1coeff = config.t1coeff;
    return;
  },
};

void wifiConfigChange() {
  persWM.setAp(config.wifi.hotspot_name, config.wifi.hotspot_password, true);
}

////////////////////////////////
///   Link functions         ///
////////////////////////////////
int initLink() {
  strcpy(terminal.buf, "#");
  ResetInternalProgrammingState(&link_state);
  pinMode(config.swio_pin, OUTPUT_OPEN_DRAIN);
  #ifdef R_GLITCH_HIGH
  gpio_set_drive_capability((gpio_num_t)config.swio_pin, GPIO_DRIVE_CAP_0);
  #endif
  
  if (config.pin3v3 >= 0) {
    pinMode(config.pin3v3, OUTPUT);
    digitalWrite(config.pin3v3, HIGH);
  }
  // gpio_set_direction((gpio_num_t)config.swio_pin, GPIO_MODE_INPUT_OUTPUT_OD);
	link_state.pinmask = 1<<config.swio_pin;
  link_state.t1coeff = config.t1coeff;

  MCFWriteReg32(&link_state, DMSHDWCFGR, 0x5aa50000 | (1<<10) ); // Shadow Config Reg
	MCFWriteReg32(&link_state, DMCFGR, 0x5aa50000 | (1<<10) ); // CFGR (1<<10 == Allow output from slave)
	MCFWriteReg32(&link_state, DMCFGR, 0x5aa50000 | (1<<10) ); // Bug in silicon?  If coming out of cold boot, and we don't do our little "song and dance" this has to be called.
  MCFWriteReg32(&link_state, DMABSTRACTAUTO, 0x00000000); // Disable Autoexec.
  // delay(10);
  int _status = 0;
  // Read back chip status.
  uint32_t reg = 0;
	int r;
  //  = MCFReadReg32(&link_state, DMSTATUS, &reg );
  int timeout = 0;
  while (timeout < WCH_MAX_TIMEOUT) {
    reg = 0;
    r = MCFReadReg32(&link_state, DMSTATUS, &reg );
    timeout++;
    if(timeout >= WCH_MAX_TIMEOUT) {
      Serial.println("Timeout on status read.");
    } 
    if(!r && !((reg & 0xc0) == 0x40 || reg == 0 || reg == 0xffffffff)) {
      break;
    }
  }
	if( r >= 0 ) {
		// Valid R.
		if( reg == 0x00000000 || reg == 0xffffffff ) {
			Serial.printf("Error: Setup chip failed. Got code %08x\n\r", reg );
			_status = -9;
		} else {
      _status = 1;
      Serial.printf("Got code %08x\n\r", reg );
    }

	} else {
		Serial.println(F("Error: Could not read chip code."));
		_status = r;
	}

	link_state.statetag = STTAG( "STRT" );
  return _status;
}

int writeBinary(uint32_t offset, uint32_t size) {
  if (size > MAX_BINARY_SIZE) {
    return -1;
  }
  // for(int i=0; i<10; i++) {
  //   Serial.print(binary_buf[i], HEX);
  // }
  // Serial.println("");
  if(initLink() < 1) return -2;
  // delay(10);
  int is_flash = ( offset & 0xff000000 ) == 0x08000000 || ( offset & 0x1FFFF800 ) == 0x1FFFF000;
  HaltMode(&link_state, is_flash?0:5);
  // delay(10);
  int flash_result = WriteBinaryBlob(&link_state, offset, size, binary_buf);
  delay(10);
  if (is_flash) {
    HaltMode(&link_state, 1);
    delay(10);
  }
  return flash_result;
}

int unbrick() {
  struct SWIOState * dev = &link_state;

  if (config.pin3v3 < 0) return -1;
  pinMode(config.pin3v3, OUTPUT);
	Serial.println("Entering Unbrick Mode");
  digitalWrite(config.pin3v3, LOW);
	delay(60);
	delay(60);
	delay(60);
	delay(60);
  digitalWrite(config.pin3v3, HIGH);
	delayMicroseconds(100);
// 	MCF.FlushLLCommands( dev );
	Serial.println("Connection starting");
	int timeout = 0;
	int max_timeout = 500;
	uint32_t ds = 0;
	for( timeout = 0; timeout < max_timeout; timeout++ )
	{
		delayMicroseconds( 10 );
		MCFWriteReg32( dev, DMSHDWCFGR, 0x5aa50000 | (1<<10) ); // Shadow Config Reg
		MCFWriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // CFGR (1<<10 == Allow output from slave)
		MCFWriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // Bug in silicon?  If coming out of cold boot, and we don't do our little "song and dance" this has to be called.
    MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
    MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Initiate a halt request.
    MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // No, really make sure.
    MCFWriteReg32( dev, DMCONTROL, 0x80000001 );
		// MCF.FlushLLCommands( dev );
		int r = MCFReadReg32( dev, DMSTATUS, &ds );
		if( r )
    {
   	  Serial.printf("Error: Could not read DMSTATUS from programmers (%d)\n\r", r);
   	  return -99;
    }
// 		MCF.FlushLLCommands( dev );
		if( ds != 0xffffffff && ds != 0x00000000 ) break;
	}

// Make sure we are in halt.
	MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
	MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Initiate a halt request.
	MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // No, really make sure.
	MCFWriteReg32( dev, DMCONTROL, 0x80000001 );

  int r = MCFReadReg32( dev, DMSTATUS, &ds );
	Serial.printf("DMStatus After Halt: /%d/%08x\n\r", r, ds);

//  Many times we would clear the halt request, but in this case, we want to just leave it here, to prevent it from booting.
//  TODO: Experiment and see if this is needed/wanted in cases.  NOTE: If you don't clear halt request, progarmmers can get stuck.
//	MCFWriteReg32( dev, DMCONTROL, 0x00000001 ); // Clear Halt Request.

// After more experimentation, it appaers to work best by not clearing the halt request.

//  MCF.FlushLLCommands( dev );

// Override all option bytes and reset to factory settings, unlocking all flash sections.
	uint8_t option_data[] = { 0xa5, 0x5a, 0x97, 0x68, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00 };
	WriteBinaryBlob(dev, 0x1ffff800, sizeof( option_data ), option_data );

	delay(20);
	if( timeout == max_timeout ) 
	{
		Serial.println("Timed out trying to unbrick");
		return -5;
	}
	EraseFlash( dev, 0, 0, 1);
// 	MCF.FlushLLCommands( dev );
	return -5;
}

void pollTerminal(void *pvParameter) {
  Serial.printf("Terminal polling is running on core %d\n\r", (int)xPortGetCoreID());
  uint32_t send_word = 0;
  while(true) {  
    if (terminal.connected) {
      if (config.uart == true) {
        if (Uart.available() > 0) {
          terminal_ws.binaryAll(String("#"+Uart.readString()));
        }
        if (terminal.incomming_buf[terminal.incomming_pos] != 0) {
          Uart.print(terminal.incomming_buf);
          terminal.incomming_pos = 0;
          terminal.incomming_buf[0] = 0;
          link_events.send("+", "terminal", millis());
        }
      } else {
        if (send_word == 0 && terminal.incomming_buf[terminal.incomming_pos] != 0) {
          int i;
          for (i=0; i<3; i++) {
            if(!terminal.incomming_buf[terminal.incomming_pos+i]) break;
            send_word |= terminal.incomming_buf[terminal.incomming_pos+i] << (i*8+8);
          }
          send_word |= i+4;
          if (terminal.incomming_buf[terminal.incomming_pos+i+1] != 0) {
            terminal.incomming_pos += i+1;
          } else {
            terminal.incomming_buf[terminal.incomming_pos] = 0;
            link_events.send("+", "terminal", millis());
          }
        }
        int r;
        uint32_t rr;
        if( link_state.statetag != STTAG( "TERM" ) )
        {
          MCFWriteReg32( &link_state, DMABSTRACTAUTO, 0x00000000 ); // Disable Autoexec.
          link_state.statetag = STTAG( "TERM" );
        }
        r = MCFReadReg32( &link_state, DMDATA0, &rr );

        if(r != 0) {
          Serial.printf("Terminal dead.  code %d\n\r", r );
          terminal_ws.closeAll();
          terminal.connected = false;
          send_word = 0;
        }
        if( rr & 0x80 ) {
          int num_printf_chars = (rr & 0xf)-4;
          if (strlen(terminal.buf) + num_printf_chars > TERMINAL_BUFFER_SIZE-1) {
            terminal_ws.binaryAll(terminal.buf);
            terminal.last_send_time = millis();
            strcpy(terminal.buf, "#");
          }
          if(num_printf_chars > 0 && num_printf_chars <= 7) {
            int firstrem = num_printf_chars;
            if( firstrem > 3 ) firstrem = 3;
            strncat(terminal.buf, ((const char*)&rr)+1, firstrem);
            if( num_printf_chars > 3 ) {
              uint32_t r2;
              r = MCFReadReg32( &link_state, DMDATA1, &r2 );
              strncat(terminal.buf, (const char*)&r2, num_printf_chars - 3);
            }
          }
          MCFWriteReg32( &link_state, DMDATA0, send_word ); // Write that we acknowledge the data.
            send_word = 0;
        }
      }
    }
    delay(1);
  }
}

int chipInfo(char* buf) {
	uint32_t reg;
  
	HaltMode(&link_state, HALT_MODE_HALT_BUT_NO_RESET);
	
	if(ReadWord(&link_state, 0x1FFFF800, &reg ) ) goto fail;	
	// printf( "USER/RDPR  : %04x/%04x\n", reg>>16, reg&0xFFFF );
	sprintf(buf, "%04x;%04x;", reg>>16, reg&0xFFFF );
	if(ReadWord(&link_state, 0x1FFFF804, &reg ) ) goto fail;	
	// printf( "DATA1/DATA0: %04x/%04x\n", reg>>16, reg&0xFFFF );
	sprintf(buf+10, "%04x;%04x;", reg>>16, reg&0xFFFF );
	if(ReadWord(&link_state, 0x1FFFF808, &reg ) ) goto fail;	
	// printf( "WRPR1/WRPR0: %04x/%04x\n", reg>>16, reg&0xFFFF );
	sprintf(buf+20, "%04x;%04x;", reg>>16, reg&0xFFFF );
	if(ReadWord(&link_state, 0x1FFFF80c, &reg ) ) goto fail;	
	// printf( "WRPR3/WRPR2: %04x/%04x\n", reg>>16, reg&0xFFFF );
	sprintf(buf+30, "%04x;%04x;", reg>>16, reg&0xFFFF );
	if(ReadWord(&link_state, 0x1FFFF7E8, &reg ) ) goto fail;	
	// printf( "R32_ESIG_UNIID1: %08x\n", reg );
	sprintf(buf+40, "%08x;", reg );
	if(ReadWord(&link_state, 0x1FFFF7EC, &reg ) ) goto fail;	
	// printf( "R32_ESIG_UNIID2: %08x\n", reg );
	sprintf(buf+49, "%08x;", reg );
	if(ReadWord(&link_state, 0x1FFFF7F0, &reg ) ) goto fail;	
	// printf( "R32_ESIG_UNIID3: %08x\n", reg );
	sprintf(buf+58, "%08x;", reg );
  if(ReadWord(&link_state, 0x1FFFF7E0, &reg ) ) goto fail;
	// printf( "Flash Size: %d kB\n", (reg&0xffff) );
	sprintf(buf+67, "%dkB", (reg&0xffff) );
  HaltMode(&link_state, HALT_MODE_RESUME);
	return 0;
fail:
	// Error: Failed to get chip details
	return -11;
}

void handleFlasher() {
  if (flasher.will_flash || flasher.will_unbrick) {
    flasher.will_flash = !flasher.will_unbrick;
    flasher.will_unbrick = !flasher.will_flash;
    terminalDisconnect();
    delay(100);
  }
  if (flasher.will_flash) {
    flasher.will_flash = false;
    int flash_result;
    for (flasher.current_retry = 0; flasher.current_retry <= flasher.retries; flasher.current_retry++) {
      flasher.watchdog = millis();
      flash_result = writeBinary(flasher.offset, flasher.size);
      if (!flash_result) break;
    }
    if (flash_result) {
      if (flash_result == -2) {
        strcpy(flasher.message, "Link init failed");  
      } else {
        sprintf(flasher.message, "Flashing failed: %d", flash_result);
      }
      flasher.error = WLF_UPDATER_ERROR;
      flasher.status = WLF_FAILED;
    } else {
      sprintf(flasher.message, "Flashed succesfully");
      flasher.status = WLF_SUCCESS;
    }
    Serial.println(flasher.message);
    link_events.send(flasher.message, "flasher", millis());
    // if (flasher_ws.active) flasher_ws.client->text(flasher.message);
    if (flasher_ws.active) flasher_ws.client->printf("#%d;%s;", flash_result?0:4, flasher.message);
    resetFlasher();
  } else if (flasher.will_unbrick) {
    flasher.will_unbrick = false;
    flasher.watchdog = millis();
    int r;
    if (config.pin3v3 < 0) {
      initLink();
      HaltMode(&link_state, 0);
      delay(10);
      r = EraseFlash(&link_state, 0, 0, 1);
      if (r) sprintf(flasher.message, "Erase failed: %d", r);
      else strcpy(flasher.message, "Success! Flash erased!");
    } else {
      r = unbrick();
      if (r) sprintf(flasher.message, "Unbrick failed: %d", r);
      else strcpy(flasher.message, "Success! Unbrick completed.");
    }
    link_events.send(flasher.message, "flasher", millis());
    Serial.println(flasher.message);
    if (flasher_ws.active) flasher_ws.client->printf("#%d;%s;", r?0:4, flasher.message);
    resetFlasher();
  } else if (flasher.will_read) {
    flasher.will_read = false;
    flasher.watchdog = millis();
    int read_result = ReadBinaryBlob(&link_state, flasher.offset, flasher.size, binary_buf);
    if (read_result) {
      Serial.println("Failed to read flash");
      if (flasher_ws.active) {
        flasher_ws.client->text("#4;Failed to read flash");
      }
    } else {
      if (flasher_ws.active) flasher_ws.client->binary(binary_buf, flasher.size);
    }
    resetFlasher();
  }
}

void parseMessage(char* message) {
  if (message[0] == 0) return;
  if (message[0] == 35) {
    if (terminal.incomming_buf[terminal.incomming_pos] == 0) {
      strncpy(terminal.incomming_buf, message+1, min(int(strlen(message-1)), (int)sizeof(terminal.incomming_buf)));
    }
  }
}

void uartSetup() {
  if (config.uart == true) {
    Uart.begin(115200);
  } else {
    Uart.end();
  }
}

void terminalDisconnect() {
  terminal.connected = false;
  terminal_ws.closeAll();
  Serial.println("Debug terminal disconnected");
}

////////////////////////////////
///   General setups         ///
////////////////////////////////
void startFS()  // Start the LittleFS and list all contents
{
  LittleFS.begin(); 
  Serial.println("LittleFS started. Contents:");
  listDir(LittleFS, "/", 0);
}

void mdnsSetup() {
  MDNS.end();
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started: http://weblink.local");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
}

void wifiSetup() {
  WiFi.hostname(String("SRL-" + device_id.substring(6,12)).c_str());
  WiFi.persistent(true);
  
  persWM.onConnect([](){ 
    server.onNotFound(onRequest);
    mdnsSetup();
    Serial.printf("This IP: %s\n\r", WiFi.localIP().toString().c_str());
    });
  //...or AP mode is started
  persWM.onAp([]() {
    AP_active = true;
    Serial.printf("AP name: %s\n\r", persWM.getApSsid().c_str());
    server.onNotFound(onRequest);
    mdnsSetup(); 
    });
  
  persWM.onApClose([]() {
    AP_active = false;
  });
  // sets network name for AP mode
  persWM.setAp(config.wifi.hotspot_name, config.wifi.hotspot_password, true);
  // persWM.setAp("WCH-WebLink", "ch32v003isfun", true);
  #if defined(WIFI_AP_NAME) && defined(WIFI_PASSWORD)
  persWM.begin(WIFI_AP_NAME, WIFI_PASSWORD);
  #else
  persWM.begin();
  #endif
  Serial.println("Wifi setup finished");
}

void setup()
{
  Serial.begin(115200);
  delay(3000);
  Serial.printf("WCH WebLink version %.2f", (float)config.sw_version/100);
  startFS();

  bool loaded = config.load(LittleFS, config_file);
  if (!loaded)
  {
    Serial.println(F("Using default config"));
    config.save(LittleFS, config_file);
  }
  // Dump config file
  config.print(LittleFS, config_file);
  config.setCallback(&configCallbacks);
  config.wifi.setCallback(&wifiConfigChange);
  
  device_id = WiFi.macAddress();
  device_id.replace(":", "");
  
  uartSetup();
  wifiSetup();
  #ifdef ARDUINO_OTA
  startOTA();
  #endif
  webServerSetup();
  
  xTaskCreatePinnedToCore(&pollTerminal, "Polling task", 10000, NULL, 0, &PollTask, xPortGetCoreID());
}

void loop()
{
  #ifdef ARDUINO_OTA
  ArduinoOTA.handle(); // listen for OTA events
  delay(1);
  #endif
  persWM.handleWiFi();
  delay(1);
  if (AP_active) {
    dns_server.processNextRequest();
    delay(1);
  }
  static long lastCleanup; 
  if (millis() - lastCleanup >= 1000) {
    terminal_ws.cleanupClients(2);
    lastCleanup = millis();
    delay(1);
  }
  if (terminal.connected) {
    uint32_t buf_len = strlen(terminal.buf);
    if (millis() - terminal.last_send_time >= config.poll_delay && buf_len > 1) {
      terminal_ws.binaryAll((const uint8_t *)terminal.buf, buf_len);
      strcpy(terminal.buf, "#");
      terminal.last_send_time = millis();
    }
  }
  if (millis() - flasher.watchdog > FLASHER_OP_TIMEOUT) {
    flasher.status = WLF_FAILED;
    flasher.error = WLF_TIMEOUT;
    resetFlasher();
  }
  delay(1);
  handleFlasher();
  delay(1);
}