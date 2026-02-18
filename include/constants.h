#pragma once

#include <Arduino.h>

// disable if you do not want to have online functionality
#define ENABLE_SERVER

#define PIN_ENABLE 4 //26
#define PIN_DATA 33 //27
#define PIN_CLOCK 13 //14
#define PIN_LATCH 5 //12
#define PIN_BUTTON 36

// disable if you do not want to use the internal storage
// https://randomnerdtutorials.com/esp32-save-data-permanently-preferences/
// timer1 on esp8266 is not compatible with flash file system reads
#define ENABLE_STORAGE

#ifdef ENABLE_SERVER
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.json
#define NTP_SERVER "de.pool.ntp.org"
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

#define COLS 16
#define ROWS 16

// Display constants
constexpr uint8_t MAX_BRIGHTNESS = 255;
constexpr uint16_t TOTAL_PIXELS = ROWS * COLS;

// set your city or coords (https://github.com/chubin/wttr.in)
#define WEATHER_LOCATION "Berlin"

// name of WiFi created by the device if no known WiFi is available
#define WIFI_MANAGER_SSID "IKEA"

// use ALL of the following to use static IP config
/*
#define IP_ADDRESS "192.168.0.250"
#define SUBNET "255.255.255.0"
#define DNS1 "1.1.1.1"
#define DNS2 "8.8.8.8"
#define GWY "192.168.0.1"
*/

// ---------------

enum SYSTEM_STATUS
{
  NONE,
  WSBINARY,
  UPDATE,
  LOADING,
};

extern volatile SYSTEM_STATUS currentStatus;
