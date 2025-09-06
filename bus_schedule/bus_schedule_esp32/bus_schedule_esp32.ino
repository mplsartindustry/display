/*

    mplsartindustry/display
    Copyright (c) 2024 held jointly by the individual authors.

    This file is part of mplsartindustry/display.

    mplsartindustry/display is free software: you can redistribute
    it and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    mplsartindustry/display is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with mplsartindustry/display.  If not, please see
    <http://www.gnu.org/licenses/>.

*/

// Setup:
// Install "esp32" board library in Boards Manager
// Install "ArduinoJson" library in Library Manager
// Set board to "ESP32 Dev Module"

// To upload over USB:
// 0. Double press reset button to enter bootloader
// 1. Upload Adafruit MatrixPortal passthrough UF2 to ATSamd51
// 2. Enable verbose output in preferences
// 3. Press verify
// 4. Copy path to merged.bin file
// 5. Run esptool.py: python3 esptool.py --before no_reset --after no_reset write_flash 0 {bin file}

// To upload over OTA (WiFi):
// 0. Code has to be uploaded over USB initially to enable OTA
// 1. Select the ESP in Tools>Port, should show as a network port
// 2. Press upload button, password is "bus"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "wifi_credentials.h"

// Comment for second display
#define IS_FIRST_DISPLAY

#ifdef IS_FIRST_DISPLAY
const char *HOSTNAME = "bus-schedule-esp32-1";
#else
const char *HOSTNAME = "bus-schedule-esp32-2";
#endif

#ifdef IS_FIRST_DISPLAY
const char *URL_1 = "https://svc.metrotransit.org/nextrip/40267"; // 40th & Lyndale Northbound: 4B
const char *URL_2 = "https://svc.metrotransit.org/nextrip/40268"; // 40th & Lyndale Southbound: 4P, 4L
const char *URL_3 = "https://svc.metrotransit.org/nextrip/1776"; // 50th & Lyndale Southbound: 4P, 4L, 46

#else
const char *URL = "https://svc.metrotransit.org/nextrip/14886"; // Grand & 40th: 113
#endif

const uint8_t MAX_BUSES = 5;

enum class ScheduleRelationship: uint8_t {
  UNKNOWN = 0,
  SCHEDULED = 1,
  SKIPPED = 2,
};

struct BusData {
  uint8_t actual;
  char tripId[64];
  char route[4];
  char terminal[4];
  char departure[16];
  ScheduleRelationship scheduleRelationship;
} __attribute__ ((packed));

struct Buses {
  BusData buses[MAX_BUSES];
  uint8_t busCount;
} __attribute__ ((packed));

struct ScheduleData {
  uint8_t stopIndex;
  uint8_t isError;
  union {
    Buses buses;
    char errorStr[128];
  };
} __attribute__ ((packed));

const uint16_t DATA_SIZE = 1 + 1 + 1 + (1 + 64 + 4 + 4 + 16 + 1) * MAX_BUSES;
static_assert(sizeof(ScheduleData) == DATA_SIZE);

const uint8_t START_BYTE = 0xA5;

const long REQUEST_DELAY = 30000;

ScheduleData response;
long prevRequestTime;

void setup() {
  Serial.begin(115200);
  delay(5000);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
  }

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword("bus");
  ArduinoOTA.begin();

  prevRequestTime = millis() - REQUEST_DELAY;
}

void putString(const char* src, char* dst, size_t dstLen) {
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

void writeResponse() {
  Serial.write(START_BYTE);
  Serial.write((uint8_t*) &response, DATA_SIZE);
}

void handleResponse(String json, uint8_t stopIndex) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    const char *errStr = error.c_str();

    response.isError = true;
    putString(errStr, response.errorStr, sizeof(response.errorStr));
    writeResponse();
    return;
  }

  response.isError = false;

  JsonArray departures = doc["departures"];
  uint8_t busCount = min(MAX_BUSES, (uint8_t) departures.size());
  uint8_t writtenCount = 0;
  for (int i = 0; i < busCount; i++) {
    JsonObject bus = departures[i];

    bool actual = bus["actual"];
    const char *tripId = bus["trip_id"];
    const char *route = bus["route_short_name"];
    const char *terminal = bus["terminal"];
    const char *departure = bus["departure_text"];
    const char *scheduleRelationship = bus["schedule_relationship"];

    // Make sure everything is there
    if (tripId == nullptr || route == nullptr || departure == nullptr) {
      continue;
    }
    if (terminal == nullptr) {
      terminal = "";
    }

    BusData *data = &response.buses.buses[writtenCount];
    data->actual = actual;
    putString(tripId, data->tripId, sizeof(data->tripId));
    putString(route, data->route, sizeof(data->route));
    putString(terminal, data->terminal, sizeof(data->terminal));
    putString(departure, data->departure, sizeof(data->departure));

    data->scheduleRelationship = ScheduleRelationship::UNKNOWN;
    if (scheduleRelationship != nullptr) {
      if (strcmp(scheduleRelationship, "Scheduled") == 0) {
        data->scheduleRelationship = ScheduleRelationship::SCHEDULED;
      } else if (strcmp(scheduleRelationship, "Skipped") == 0) {
        data->scheduleRelationship = ScheduleRelationship::SKIPPED;
      }
    }

    writtenCount++;
  }
  response.buses.busCount = writtenCount;

  writeResponse();
}

void doRequest(const char* url, uint8_t stopIndex) {
  response.stopIndex = stopIndex;
  WiFiClientSecure *client = new WiFiClientSecure;
  if (client) {
    // MetroTransit requires HTTPS, but I don't want to deal with certs
    client->setInsecure();

    {
      HTTPClient https;

      if (https.begin(*client, url)) {
        int code = https.GET();

        if (code > 0) {
          if (code == HTTP_CODE_OK) {
            String json = https.getString();
            handleResponse(json, stopIndex);
          } else {
            response.isError = true;
            snprintf(response.errorStr, sizeof(response.errorStr), "HTTP code %d", code);
            writeResponse();
          }
        } else {
          response.isError = true;
          snprintf(response.errorStr, sizeof(response.errorStr), "HTTPC err %d", code);
          writeResponse();
        }

        https.end();
      } else {
        response.isError = true;
        snprintf(response.errorStr, sizeof(response.errorStr), "connect failed");
        writeResponse();
      }
    }

    delete client;
  } else {
    response.isError = true;
    snprintf(response.errorStr, sizeof(response.errorStr), "client failed");
    writeResponse();
  }
}

void loop() {
  ArduinoOTA.handle();

  if (millis() - prevRequestTime >= REQUEST_DELAY) {
#ifdef IS_FIRST_DISPLAY
    doRequest(URL_1, 0);
    doRequest(URL_2, 1);
    doRequest(URL_3, 2);
#else
    doRequest(URL, 0);
#endif
    prevRequestTime = millis();
  }
}
