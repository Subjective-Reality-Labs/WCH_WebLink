#ifndef SRLCONFIG_H
#define SRLCONFIG_H

#include "SRLConfig.h"

SRLConfig::SRLConfig() {

}

bool SRLConfig::save(FS& fs, const char* filename) {
  // Open file for writing
  File file = fs.open(filename, "w");
  if (!file)
  {
    Serial.println(F("Failed to create config file"));
    return false;
  }
  // Serialize JSON to file
  bool success = serialize(file, true);
  if (!success)
  {
    return false;
    Serial.println(F("Failed to serialize configuration"));
  }
  return true;
}

bool SRLConfig::load(FS& fs, const char* filename) {
   // Open file for reading
  File file = fs.open(filename, "r");
  // This may fail if the file is missing
  if (!file)
  {
    Serial.println(F("Failed to open config file"));
    return false;
  }
  // Parse the JSON object in the file
  bool success = deserialize(file);
  // This may fail if the JSON is invalid
  if (!success)
  {
    Serial.println(F("Failed to deserialize configuration"));
    return false;
  }
  return true;
}

void SRLConfig::print(FS& fs, const char* filename) {
  // Open file for reading
  File file = fs.open(filename, "r");
  if (!file)
  {
    Serial.println(F("Failed to open config file"));
    return;
  }
  // Extract each by one by one
  while (file.available())
  {
    Serial.print((char)file.read());
  }
  Serial.println();
}

bool SRLConfig::serialize(Print &dst, bool pretty) {
  JsonDocument doc;

  // Create an object at the root
  JsonObject root = doc.to<JsonObject>();

  // Fill the object
  this->toJson(root);

  // Serialize JSON to file
  if (pretty) {
    return serializeJsonPretty(doc, dst) > 0;  
  } else {
    return serializeJson(doc, dst) > 0;
  }
  
}

bool SRLConfig::deserialize(Stream &src) {
  
  JsonDocument doc;

  // Parse the JSON object in the file
  DeserializationError err = deserializeJson(doc, src);
  if (err)
    return false;
  this->fromJson(doc.as<JsonObject>());
  return true;
}

void SRLConfig::setCallback(void (*cb)(void)) {
  changeCallback = cb;
}

WifiConfig::WifiConfig(){}

void WifiConfig::toJson(JsonObject obj) const {
  obj["hotspot_name"] = hotspot_name;
  obj["hotspot_password"] = hotspot_password;
}

void WifiConfig::fromJson(JsonObjectConst obj) {
  bool changed = false;
  
  if (strcmp(hotspot_name, obj["hotspot_name"])) {
    changed = true;
    strlcpy(hotspot_name, obj["hotspot_name"] | HOTSPOT_NAME, sizeof(hotspot_name));
  }

  if (strcmp(hotspot_password, obj["hotspot_password"])) {
    changed = true;
    strlcpy(hotspot_password, obj["hotspot_password"] | HOTSPOT_PASSWORD, sizeof(hotspot_password));
  }
  if (changed) changeCallback();
}

ConfigG::ConfigG(){}

void ConfigG::fromJson(JsonObjectConst obj) {
  // Read "wifi" object
  wifi.fromJson(obj["wifi"]);

  if (uart != obj["uart"].as<bool>()) {
    uart = obj["uart"].as<bool>();
    if (callbacks.uart_cb != nullptr) callbacks.uart_cb();
  }
  
  if (swio_pin != obj["swio_pin"].as<int>()) {
    if (obj["swio_pin"].isNull() || obj["swio_pin"].as<const char>() == 0) swio_pin = -1;
    else swio_pin = obj["swio_pin"].as<int>();
    if (callbacks.swio_pin_cb != nullptr) callbacks.swio_pin_cb();
  }

  if (pin3v3 != obj["pin3v3"].as<int>()) {
    if (obj["pin3v3"].isNull() || obj["pin3v3"].as<const char>() == 0) pin3v3 = -1;
    else pin3v3 = obj["pin3v3"].as<int>();
  }

  if (t1coeff != obj["t1coeff"].as<uint16_t>()) {
    t1coeff = obj["t1coeff"].as<uint16_t>();
    if (t1coeff < 2) t1coeff = 2;
    if (callbacks.t1coeff_cb != nullptr) callbacks.t1coeff_cb();
  }

  if (poll_delay != obj["poll_delay"].as<uint32_t>()) {
    poll_delay = obj["poll_delay"].as<uint32_t>();
  }

}

void ConfigG::toJson(JsonObject obj) const {
  // Add "wifi" object
  wifi.toJson(obj["wifi"].to<JsonObject>());

  obj["uart"] = uart;
  obj["swio_pin"] = swio_pin;
  obj["pin3v3"] = pin3v3;
  obj["t1coeff"] = t1coeff;
  obj["poll_delay"] = poll_delay;
  obj["sw_version"] = sw_version;
}

void ConfigG::setCallback(changeCallbacks *cbs) {
  if (cbs->uart_cb != nullptr) {
    callbacks.uart_cb = cbs->uart_cb;
  }
  if (cbs->swio_pin_cb != nullptr) {
    callbacks.swio_pin_cb = cbs->swio_pin_cb;
  }
  if (cbs->t1coeff_cb != nullptr) {
    callbacks.t1coeff_cb = cbs->t1coeff_cb;
  }
}

#endif