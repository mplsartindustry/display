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

#include <Adafruit_Protomatter.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h>

#include "wifi_credentials.h"

uint8_t rgbPins[]  = {7, 8, 9, 10, 11, 12};
uint8_t addrPins[] = {17, 18, 19, 20, 21};
const uint8_t clockPin   = 14;
const uint8_t latchPin   = 15;
const uint8_t oePin      = 16;

Adafruit_Protomatter matrix(128, 4, 1, rgbPins, 4, addrPins, clockPin, latchPin, oePin, false);
WiFiSSLClient client;

#define API_HOST "svc.metrotransit.org"

// Up to 10 chars long to fit (until smaller font)
#define STOP_1_NAME "40th & Lyn"
#define STOP_2_NAME "46th & Lyn"

#define ANIM_INTERVAL 100

// Requests alternate between the two stops
// Actual interval is request*anim in milliseconds + time to redraw
#define REQUEST_INTERVAL 200

// TODO: Make bigger once I add smaller font
// 1 more than fits on screen
#define MAX_BUSES 4
#define ROW_HEIGHT 8

struct BusInfo {
  bool present;
  int currentY;
  
  const char *route;
  const char *terminal;
  const char *timeText;

  const char *tripId; // Unique per actual bus

  int busNameLen;

  void stepTowards(int targetY) {
    if (this->currentY < targetY)
      this->currentY++;
    else if (this->currentY > targetY)
      this->currentY--;
  }
};

struct BusInfoSet {
  JsonDocument doc; // Need to keep document around so strings in this.buses stay valid
  BusInfo buses[MAX_BUSES];
  int longestRoute;

  BusInfoSet() {
    for (int i = 0; i < MAX_BUSES; i++) {
      this->buses[i].present = false;
      this->buses[i].currentY = (i + 1) * ROW_HEIGHT;
    }
  }

  void stepAll() {
    for (int i = 0; i < MAX_BUSES; i++) {
      this->buses[i].stepTowards((i + 1) * ROW_HEIGHT);
    }
  }
};

struct BusSchedule {
  int baseX;
  const char *stopName;
  const char *apiPath;
  BusInfoSet *info, *info2;
  bool newData, info2Valid;

  BusSchedule(int _baseX, const char *_stopName, const char *_apiPath)
  : baseX(_baseX), stopName(_stopName), apiPath(_apiPath) {
    this->info = new BusInfoSet();
    this->info2 = new BusInfoSet();
    this->newData = false;
    this->info2Valid = false;
  }

  void draw() {
    matrix.setTextWrap(false);

    matrix.fillRect(this->baseX, 0, 64, ROW_HEIGHT - 1, 0); // Clear behind title
    if (this->newData) {
      matrix.fillRect(this->baseX, ROW_HEIGHT, 64, 32 - ROW_HEIGHT, 0); // Clear behind buses
      this->newData = false;
    }
    matrix.drawFastHLine(this->baseX, ROW_HEIGHT - 1, 64, matrix.color565(64, 64, 64)); // Underline
    matrix.setCursor(this->baseX, 0);
    matrix.print(this->stopName);

    this->info->stepAll();
    // Iterate in reverse so buses that are sooner are rendered on top
    for (int i = MAX_BUSES - 1; i >= 0; i--) {
      BusInfo* bus = &this->info->buses[i];
      matrix.fillRect(this->baseX, bus->currentY, 64, ROW_HEIGHT, 0); // Clear behind text
      if (!bus->present)
        continue;

      matrix.setCursor(this->baseX, bus->currentY);
      matrix.print(bus->route);
      matrix.print(bus->terminal);
      matrix.print(": ");
  
      // Align the times
      for (int i = bus->busNameLen; i < this->info->longestRoute; i++)
        matrix.write(' ');
  
      matrix.print(bus->timeText);
    }
  
    matrix.show();
  }

  void handleResponse() {
    // Swap infos
    BusInfoSet *tmp = this->info;
    this->info = this->info2;
    this->info2 = tmp;
    
    DeserializationError error = deserializeJson(this->info->doc, client);
    if (error) {
      Serial.print("Deserialization error: ");
      Serial.println(error.f_str());
      return;
    }

    JsonArray departures = this->info->doc["departures"];
    int departureCount = min(departures.size(), MAX_BUSES);

    int longestRoute = 0;
    for (int i = 0; i < departureCount; i++) {
      JsonObject departure = departures[i];
      const char* routeName = departure["route_short_name"];
      const char* terminal = departure["terminal"];
      const char* departureText = departure["departure_text"];
      const char* tripId = departure["trip_id"];
  
      int routeLen = strlen(routeName);
      int terminalLen = strlen(terminal);
      int busNameLen = routeLen + terminalLen;
      longestRoute = max(longestRoute, longestRoute);

      BusInfo *bus = &this->info->buses[i];
      bus->route = routeName;
      bus->terminal = terminal;
      bus->timeText = departureText;
      bus->busNameLen = busNameLen;
      bus->tripId = tripId;
      bus->present = true;

      // Copy previous position of this bus in the list
      if (info2Valid) {
        for (int j = 0; j < MAX_BUSES; j++) {
          if (strcmp(tripId, this->info2->buses[j].tripId) == 0) {
            bus->currentY = this->info2->buses[j].currentY;
          }
        }
      }
    }
    this->info->longestRoute = longestRoute;

    for (int i = departureCount; i < MAX_BUSES; i++) {
      this->info->buses[i].present = false;
    }

    this->info2Valid = true;
    this->newData = true;
  }

  void doRequest() {
    Serial.print("Requesting stop data from ");
    Serial.println(this->apiPath);
    if (client.connect(API_HOST, 443)) {
      Serial.println("connected to server");
  
      client.print("GET ");
      client.print(this->apiPath);
      client.println(" HTTP/1.1");
      client.println("Host: " API_HOST);
      client.println("Connection: close");
      client.println();
  
      bool prevWasNewline = false;
      bool parsed = false;
      while (true) {
        while (client.available()) {
          char c = client.read();
  
          // If already parsed the content, skip any remaining bytes
          if (parsed) continue;
          
          // Skip HTTP headers
          // TODO: Should probably check the status code
          if (c == '\n') {
            if (prevWasNewline) {
              this->handleResponse();
              parsed = true;
              break;
            }
            prevWasNewline = true;
          } else if (c != '\r') {
            prevWasNewline = false;
          }
        }
    
        if (!client.connected()) {
          Serial.println();
          Serial.println("disconnecting from server.");
          client.stop();
          break;
        }
      }
    } else {
      Serial.println("Client failed to connect");
    }
  }
};

//void handleResponse(boolean isStop2) {
//  
//
//  
//
//  // Get bus info from the JSON
//  BusInfo buses[MAX_BUSES];
//  int longestRoute = 0;
//  for (int i = 0; i < departureCount; i++) {
//    JsonObject departure = departures[i];
//    const char* routeName = departure["route_short_name"];
//    const char* terminal = departure["terminal"];
//    const char* departureText = departure["departure_text"];
//
//    int routeLen = strlen(routeName);
//    int terminalLen = strlen(terminal);
//    int busNameLen = routeLen + terminalLen;
//    longestRoute = max(longestRoute, longestRoute);
//
//    buses[i].route = routeName;
//    buses[i].terminal = terminal;
//    buses[i].timeText = departureText;
//    buses[i].busNameLen = busNameLen;
//  }
//
//  matrix.setTextWrap(false);
//
//  int baseX = isStop2 ? 64 : 0;
//  matrix.fillRect(baseX, 0, 64, 32, 0); // Clear background
//  matrix.drawFastHLine(baseX, ROW_HEIGHT - 1, 64, matrix.color565(64, 64, 64));
//  matrix.setCursor(baseX, 0);
//  if (isStop2) {
//    matrix.print(STOP_2_NAME);
//  } else {
//    matrix.print(STOP_1_NAME);
//  }
//
//  for (int i = 0; i < departureCount; i++) {
//    BusInfo* bus = &buses[i];
//    matrix.setCursor(baseX, (i + 1) * ROW_HEIGHT);
//    matrix.print(bus->route);
//    matrix.print(bus->terminal);
//    matrix.print(": ");
//
//    // Align the times
//    for (int i = bus->busNameLen; i < longestRoute; i++)
//      matrix.write(' ');
//
//    matrix.print(bus->timeText);
//  }
//
//  matrix.show();
//}
//
//void doRequest(boolean isStop2) {
//  
//}

BusSchedule stop1(0, "40th & Lyn", "/nextrip/40268");
BusSchedule stop2(64, "46th & Lyn", "/nextrip/2855");

void setup() {
  Serial.begin(9600);
  delay(2000);

  // Initialize matrix
  ProtomatterStatus matrixStatus = matrix.begin();
  while (matrixStatus != PROTOMATTER_OK) {
    Serial.println("Protomatter begin failed");
    delay(1000);
  }

  // Connect to WiFi
  matrix.println("Connecting to WiFi");
  matrix.show();
  while (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module failed");
    delay(1000);
  }
  Serial.println("Connecting to wifi..");
  int wifiStatus;
  do {
    wifiStatus = WiFi.begin(wifiSsid, wifiPass);
    delay(100);
  } while (wifiStatus != WL_CONNECTED);
  Serial.println("Wifi connected");

//  doRequest(false);
//  doRequest(true);
  matrix.println("Getting data");
  matrix.show();
  stop1.doRequest();
  stop2.doRequest();
}

void delayAndAnimate() {
  for (int i = 0; i < REQUEST_INTERVAL; i++) {
    stop1.draw();
    stop2.draw();
    delay(ANIM_INTERVAL);
  }
}

void loop() {
  delayAndAnimate();
  stop1.doRequest();
  delayAndAnimate();
  stop2.doRequest();
}
