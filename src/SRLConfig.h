#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>

#define HOTSPOT_NAME "WebLink"
#define HOTSPOT_PASSWORD "ch32v003isfun"
#ifndef SW_VERSION
#define SW_VERSION 70
#endif
#ifndef SWIO_PIN
#define SWIO_PIN 10
#endif
#define TERMINAL_SEND_DELAY 1000
#ifndef DEFAULT_T1COEFF
#define DEFAULT_T1COEFF 7
#endif

class SRLConfig {

  public:

    SRLConfig();

    bool save(FS&, const char* filename);

    bool load(FS&, const char* filename);

    void print(FS&, const char* filename);

    bool serialize(Print &dst, bool pretty=false);
    
    bool deserialize(Stream &src);

    virtual void fromJson(JsonObjectConst) = 0;
    
    virtual void toJson(JsonObject) const = 0;

    virtual void setCallback(void (*cb)(void));


  protected:

    void (*changeCallback)(void);
};

class WifiConfig : public SRLConfig {
  
  public:

    WifiConfig();

    void fromJson(JsonObjectConst);
    
    void toJson(JsonObject) const;

    char hotspot_name[32] = HOTSPOT_NAME;
    char hotspot_password[64] = HOTSPOT_PASSWORD;
};

class ConfigG : public SRLConfig {
  
  public:

    struct changeCallbacks {
      void (*uart_cb)(void);
      void (*swio_pin_cb)(void);
      void (*t1coeff_cb)(void);
    } callbacks;

    ConfigG();

    void fromJson(JsonObjectConst);
    
    void toJson(JsonObject) const;

    void setCallback(changeCallbacks *cbs);

    WifiConfig wifi;
    bool uart = false;
    int swio_pin = SWIO_PIN;
    int pin3v3 = -1;
    uint16_t t1coeff = DEFAULT_T1COEFF;
    uint32_t poll_delay = TERMINAL_SEND_DELAY;
    const unsigned int sw_version = SW_VERSION;

};