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

// Set board to "ESP32 Dev Module"

// To upload over USB:
// 0. Upload Adafruit MatrixPortal passthrough UF2 to ATSamd51
// 1. Enable verbose output in preferences
// 2. Press verify
// 3. Copy path to merged.bin file
// 4. Run esptool.py: python3 esptool.py --before no_reset --after no_reset write_flash 0 {bin file}

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

const char *URL_1 = "https://svc.metrotransit.org/nextrip/40268";
const char *URL_2 = "https://svc.metrotransit.org/nextrip/2855";

const uint8_t MAX_BUSES = 5;

struct BusData {
  uint8_t actual;
  char tripId[64];
  char route[4];
  char terminal[4];
  char departure[16];
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

const uint16_t DATA_SIZE = 1 + 1 + 1 + (1 + 64 + 4 + 4 + 16) * MAX_BUSES;
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

  ArduinoOTA.setHostname("bus-schedule-esp32");
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

  response.stopIndex = stopIndex;
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

    // Make sure everything is there
    if (tripId == nullptr || route == nullptr || terminal == nullptr || departure == nullptr) {
      continue;
    }

    BusData *data = &response.buses.buses[writtenCount];
    data->actual = actual;
    putString(tripId, data->tripId, sizeof(data->tripId));
    putString(route, data->route, sizeof(data->route));
    putString(terminal, data->terminal, sizeof(data->terminal));
    putString(departure, data->departure, sizeof(data->departure));

    writtenCount++;
  }
  response.buses.busCount = writtenCount;

  writeResponse();
}

void doRequest(const char* url, uint8_t stopIndex) {
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
    doRequest(URL_1, 0);
    doRequest(URL_2, 1);
    prevRequestTime = millis();
  }
}
