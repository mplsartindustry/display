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
#include <Fonts/Picopixel.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h>

#include "wifi_credentials.h"
#include "anim.h"

uint8_t rgbPins[]  = {7, 8, 9, 10, 11, 12};
uint8_t addrPins[] = {17, 18, 19, 20, 21};
const uint8_t clockPin   = 14;
const uint8_t latchPin   = 15;
const uint8_t oePin      = 16;

Adafruit_Protomatter matrix(128, 4, 1, rgbPins, 4, addrPins, clockPin, latchPin, oePin, false);
WiFiSSLClient client;

const uint8_t COLOR_COUNT = 7;
uint16_t busColors[COLOR_COUNT] = {
  matrix.color565(255, 0, 0),
  matrix.color565(255, 128, 0),
  matrix.color565(255, 255, 0),
  matrix.color565(0, 255, 0),
  matrix.color565(0, 255, 255),
  matrix.color565(0, 0, 255),
  matrix.color565(128, 0, 255),
};

#define API_HOST "svc.metrotransit.org"

#define ANIM_INTERVAL 100

// Requests alternate between the two stops
// Actual interval is request*anim in milliseconds + time to redraw
#define REQUEST_INTERVAL 192

// TODO: Make bigger once I add smaller font
// 1 more than fits on screen
#define MAX_BUSES 5
#define ROW_HEIGHT 6
#define ASCENT 4

struct AssignedColor {
  char tripId[128];
  uint16_t color;
};

struct TripColors {
  AssignedColor assignedColors[COLOR_COUNT];
  int8_t filled[COLOR_COUNT];
  uint8_t nextColorIdx;

  TripColors(): nextColorIdx(0) {
    for (uint8_t i = 0; i < COLOR_COUNT; i++) {
      filled[i] = -1;
    }
  }

  uint16_t getColor(const char *tripId) {
    // Find already assigned one with this trip id
    for (uint8_t i = 0; i < COLOR_COUNT; i++) {
      if (filled[i] >= 0 && strcmp(tripId, assignedColors[i].tripId) == 0) {
        // Trip ID matches
        return assignedColors[i].color;
      }
    }

    // Decrement other slots. This will discard the oldest entry
    for (uint8_t i = 0; i < COLOR_COUNT; i++) {
      if (filled[i] >= 0) {
        filled[i] -= 1;
      }
    }

    // Search for empty slot
    for (uint8_t i = 0; i < COLOR_COUNT; i++) {
      if (filled[i] < 0) {
        uint16_t color = busColors[nextColorIdx++];
        if (nextColorIdx >= COLOR_COUNT)
          nextColorIdx -= COLOR_COUNT;

        filled[i] = COLOR_COUNT - 1;
        assignedColors[i].color = color;
        strcpy(assignedColors[i].tripId, tripId);

        return color;
      }
    }

    return matrix.color565(255, 255, 255);
  }
};

TripColors tripColors;

struct BusInfo {
  bool present;
  int currentY;
  
  const char *route;
  const char *terminal;
  const char *timeText;

  const char *tripId; // Unique per actual bus

  int busNameLen;
  uint16_t color;
  bool actual;

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

  DeserializationError error;

  BusInfoSet() {
    for (int i = 0; i < MAX_BUSES; i++) {
      this->buses[i].present = false;
      this->buses[i].currentY = (i + 1) * ROW_HEIGHT + 2;
    }
  }

  void stepAll() {
    for (int i = 0; i < MAX_BUSES; i++) {
      this->buses[i].stepTowards((i + 1) * ROW_HEIGHT + 2);
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

  void draw(float progressPct) {
    matrix.setTextWrap(false);

    matrix.fillRect(this->baseX, 0, 64, ROW_HEIGHT - 1, 0); // Clear behind title
    if (this->newData) {
      matrix.fillRect(this->baseX, ROW_HEIGHT, 64, 32 - ROW_HEIGHT, 0); // Clear behind buses
      this->newData = false;
    }
    int progressPt = (int) (64 - 64 * progressPct);
    matrix.drawFastHLine(this->baseX, ROW_HEIGHT - 1, progressPt, matrix.color565(64, 64, 64)); // Underline
    matrix.drawFastHLine(this->baseX + progressPt, ROW_HEIGHT - 1, 64 - progressPt, matrix.color565(16, 16, 16));
    matrix.setCursor(this->baseX + 3, ASCENT);
    matrix.setTextColor(matrix.color565(255, 255, 255));
    matrix.print(this->stopName);

    if (this->info->error) {
      matrix.setTextColor(matrix.color565(255, 0, 0));
      matrix.setCursor(this->baseX + 3, ASCENT + ROW_HEIGHT);
      matrix.print("JSON Error:");
      matrix.setCursor(this->baseX + 3, ASCENT + ROW_HEIGHT * 2);
      matrix.print(this->info->error.f_str());
      return;
    }

    this->info->stepAll();
    // Iterate in reverse so buses that are sooner are rendered on top
    for (int i = MAX_BUSES - 1; i >= 0; i--) {
      BusInfo* bus = &this->info->buses[i];
      matrix.fillRect(this->baseX, bus->currentY, 64, ROW_HEIGHT, 0); // Clear behind text
      if (!bus->present)
        continue;

      if (bus->actual) {
        matrix.fillRect(this->baseX, bus->currentY, 2, ROW_HEIGHT - 1, bus->color);
      }

      matrix.setCursor(this->baseX + 3, bus->currentY + ASCENT);
      matrix.setTextColor(bus->color);
      matrix.print(bus->route);
      matrix.print(bus->terminal);
      matrix.print(":  ");
  
      // Align the times
      for (int i = bus->busNameLen; i <= this->info->longestRoute; i++)
        matrix.write(' ');
  
      matrix.print(bus->timeText);
    }
  }

  void handleResponse() {
    // Swap infos
    BusInfoSet *tmp = this->info;
    this->info = this->info2;
    this->info2 = tmp;

    Serial.println("Infos swapped, deserializing");
    
    DeserializationError error = deserializeJson(this->info->doc, client);
    this->info->error = error;
    if (error) {
      Serial.print("Deserialization error: ");
      Serial.println(error.f_str());
      return;
    }
    Serial.println("Deserialized");

    JsonArray departures = this->info->doc["departures"];
    int departureCount = min(departures.size(), MAX_BUSES);

    Serial.println("Got departure count: ");
    Serial.println(departureCount);

    int longestRoute = 0;
    for (int i = 0; i < departureCount; i++) {
      Serial.print("Deserializing ");
      Serial.println(i);
      JsonObject departure = departures[i];
      const char* routeName = departure["route_short_name"];
      const char* terminal = departure["terminal"];
      const char* departureText = departure["departure_text"];
      const char* tripId = departure["trip_id"];
      bool actual = departure["actual"];

      Serial.println("Got the values");
  
      int routeLen = strlen(routeName);
      int terminalLen = strlen(terminal);
      int busNameLen = routeLen + terminalLen;
      longestRoute = max(busNameLen, longestRoute);

      BusInfo *bus = &this->info->buses[i];
      bus->route = routeName;
      bus->terminal = terminal;
      bus->timeText = departureText;
      bus->busNameLen = busNameLen;
      bus->tripId = tripId;
      bus->present = true;
      bus->color = tripColors.getColor(tripId);
      bus->actual = actual;

      Serial.println("Set the values");
      Serial.println("This bus is called:");
      Serial.println(tripId);

      // Copy previous position of this bus in the list
      if (info2Valid) {
        for (int j = 0; j < MAX_BUSES; j++) {
          if (this->info2->buses[j].present && strcmp(tripId, this->info2->buses[j].tripId) == 0) {
            bus->currentY = this->info2->buses[j].currentY;
          }
        }
      }

      Serial.println("Done with this bus");
    }
    this->info->longestRoute = longestRoute;

    for (int i = departureCount; i < MAX_BUSES; i++) {
      this->info->buses[i].present = false;
    }

    this->info2Valid = true;
    this->newData = true;

    Serial.println("Done handling!");
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
      bool chunkHasBegan = false;
      bool parsed = false;
      while (true) {
        while (client.available()) {
          char c = client.read();

          Serial.write(c);
  
          // If already parsed the content, skip any remaining bytes
          if (parsed) continue;
          
          // Skip HTTP headers
          // TODO: Should probably check the status code
          if (c == '\n') {
            // The response is sent as chunked so need to skip the chunk length header also
            if (chunkHasBegan) {
              // Serial.println("Handling response");
              this->handleResponse();
              parsed = true;
              break;
            }
            if (prevWasNewline) {
              // Serial.println("Chunk began");
              chunkHasBegan = true;
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

BusSchedule stop1(0, "40th and Lyndale", "/nextrip/40268");
BusSchedule stop2(64, "46th and Lyndale", "/nextrip/2855");

void setup() {
  Serial.begin(9600);
  delay(2000);

  // Initialize matrix
  ProtomatterStatus matrixStatus = matrix.begin();
  while (matrixStatus != PROTOMATTER_OK) {
    Serial.println("Protomatter begin failed");
    delay(1000);
  }

  matrix.setFont(&Picopixel);

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

  matrix.println("Getting data");
  matrix.show();
  stop1.doRequest();
  stop2.doRequest();
}

uint8_t animCounter = 0;
uint8_t animFrame = 0;

void drawAnimation(int x, int y) {
  // Slow down!
  if (animCounter == 0) {
    animCounter = 1;
    animFrame++;
    animFrame %= ANIMATION_LEN;
  } else {
    animCounter--;
  }

  uint16_t color = matrix.color565(64, 64, 64);
  for (uint8_t colIdx = 0; colIdx < ANIMATION_WIDTH; colIdx++) {
    uint16_t colData = getAnimationColumn(animFrame, colIdx);
    for (uint8_t i = 0; i < 16; i++) {
      if (colData & (1 << i)) {
        matrix.drawPixel(x + colIdx, y + (15 - i), color);
      }
    }
  }
}

void delayAndAnimate(bool isStop1) {
  for (int i = 0; i < REQUEST_INTERVAL; i++) {
    float pct = (float) (i + 1) / REQUEST_INTERVAL;
    stop1.draw(isStop1 ? pct : 0);
    stop2.draw(isStop1 ? 0 : pct);
    drawAnimation(46, 12);
    matrix.show();
    delay(ANIM_INTERVAL);
  }
}

void loop() {
  Serial.println("A");
  delayAndAnimate(true);
  Serial.println("B");
  stop1.doRequest();
  Serial.println("C");
  delayAndAnimate(false);
  Serial.println("D");
  stop2.doRequest();
  Serial.println("E");
}
