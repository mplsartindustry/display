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

// Add Adafruit board manager URL: https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
// Install "Adafruit SAMD Boards" in Board Manager
// Install "Adafruit Protomatter" in Library Manager
// Set board to "Adafruit Matrix Portal M4"

#include <Adafruit_Protomatter.h>
#include <Fonts/Picopixel.h>

// Comment for second display
#define IS_FIRST_DISPLAY

const uint8_t MAX_BUSES = 5;
const uint8_t ROW_HEIGHT = 6;
const uint8_t ASCENT = 4;

const uint8_t START_BYTE = 0xA5;
const uint16_t DATA_SIZE = 1 + 1 + 1 + (1 + 64 + 4 + 4 + 16 + 1) * MAX_BUSES;
const int SERIAL_BAUD = 115200;
const int SERIAL_BYTES_PER_LOOP = 115200 / 8 / 60;

uint8_t rgbPins[]  = {7, 8, 9, 10, 11, 12};
uint8_t addrPins[] = {17, 18, 19, 20, 21};
const uint8_t clockPin   = 14;
const uint8_t latchPin   = 15;
const uint8_t oePin      = 16;
#ifdef IS_FIRST_DISPLAY
const uint16_t bitWidth = 128;
#else
const uint16_t bitWidth = 64;
#endif
Adafruit_Protomatter matrix(bitWidth, 4, 1, rgbPins, 4, addrPins, clockPin, latchPin, oePin, false);

const uint8_t COLOR_COUNT = 7;
uint16_t busColors[COLOR_COUNT] = {
  matrix.color565(255, 0, 0),
  matrix.color565(255, 128, 0),
  matrix.color565(255, 255, 0),
  matrix.color565(0, 255, 0),
  matrix.color565(0, 255, 255),
  matrix.color565(16, 32, 255),
  matrix.color565(128, 32, 255),
};

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
static_assert(sizeof(ScheduleData) == DATA_SIZE);


struct AssignedColor {
  char tripId[64];
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

struct BusInfo {
  int currentY;
  uint16_t color;
  BusData data;
  
  void stepTowards(int y) {
    if (currentY < y)
      currentY++;
    if (currentY > y)
      currentY--;
  }
};

struct BusInfoSet {
  BusInfo buses[MAX_BUSES];
  uint8_t busCount;

  bool hasError;
  char errorStr[128];

  uint16_t longestNamePx;

  BusInfoSet() {
    busCount = 0;
    hasError = false;
    longestNamePx = 0;
  }

  int rowY(int i) {
    return (i + 1) * ROW_HEIGHT + 2;
  }

  void resetPositions() {
    for (int i = 0; i < MAX_BUSES; i++) {
      buses[i].currentY = rowY(i);
    }
  }

  void stepPositions() {
    for (int i = 0; i < busCount; i++) {
      buses[i].stepTowards(rowY(i));
    }
  }
};

struct BusSchedule {
  int baseX;
  int width;
  const char *stopName;
  TripColors *tripColors;
  BusInfoSet *info, *info2;
  int animTimer;
  bool hasData;

  BusSchedule(int _baseX, int _width, const char *_stopName, TripColors *_tripColors)
  : baseX(_baseX), width(_width), stopName(_stopName), tripColors(_tripColors) {
    info = new BusInfoSet();
    info2 = new BusInfoSet();
    animTimer = 10;
    hasData = false;
  }

  void draw() {
    animTimer--;
    if (animTimer == 0) {
      animTimer = 10;
      info->stepPositions();
    }

    matrix.setTextWrap(false);

    matrix.drawFastHLine(baseX, ROW_HEIGHT - 1, width, matrix.color565(64, 64, 64));
    matrix.setCursor(baseX + 3, ASCENT);
    matrix.setTextColor(0xFFFF);
    matrix.print(stopName);

    if (!hasData) {
      matrix.setTextColor(matrix.color565(48, 48, 48));
      matrix.setCursor(baseX + 3, ASCENT + ROW_HEIGHT + 2);
      matrix.print("Loading...");
      return;
    }

    if (info->hasError) {
      matrix.setTextColor(matrix.color565(255, 0, 0));
      matrix.setCursor(baseX + 3, ASCENT + ROW_HEIGHT + 2);
      matrix.print("Error:");
      matrix.setCursor(baseX + 3, ASCENT + ROW_HEIGHT * 2 + 2);
      matrix.print(info->errorStr);
      return;
    }

    if (info->busCount == 0) {
      matrix.setTextColor(matrix.color565(48, 48, 48));
      matrix.setCursor(baseX + 3, ASCENT + ROW_HEIGHT * 2 + ROW_HEIGHT / 2 + 1);
      matrix.print("No buses :(");
      return;
    }

    for (int i = info->busCount - 1; i >= 0; i--) {
      BusInfo *bus = &info->buses[i];

      int y = bus->currentY;

      // Clear behind text
      matrix.fillRect(baseX, y, width, ROW_HEIGHT, 0);

      bool skipped = bus->data.scheduleRelationship == ScheduleRelationship::SKIPPED;
      uint16_t color = skipped ? matrix.color565(48, 0, 0) : bus->color;

      if (bus->data.actual)
        matrix.fillRect(baseX, y, 2, ROW_HEIGHT - 1, color);
    
      matrix.setCursor(baseX + 3, y + ASCENT);
      matrix.setTextColor(color);
      matrix.print(bus->data.route);
      matrix.print(bus->data.terminal);
      matrix.print(":");
      matrix.setCursor(baseX + info->longestNamePx + 9, y + ASCENT);
      matrix.print(bus->data.departure);

      if (skipped) {
        matrix.drawFastHLine(baseX, y + 2, width - 1, color);
      }
    }
  }

  void handleResponse(ScheduleData *data) {
    BusInfoSet *tmp = info;
    info = info2;
    info2 = tmp;
    info->resetPositions();

    hasData = true;

    if (data->isError) {
      info->hasError = true;
      strcpy(info->errorStr, data->errorStr);
      return;
    }

    info->longestNamePx = 0;
    info->hasError = false;
    info->busCount = data->buses.busCount;
    for (int i = 0; i < info->busCount; i++) {
      BusInfo *bus = &info->buses[i];
      bus->data = data->buses.buses[i];
      bus->color = tripColors->getColor(bus->data.tripId);

      for (int j = 0; j < info2->busCount; j++) {
        if (strcmp(bus->data.tripId, info2->buses[j].data.tripId) == 0) {
          bus->currentY = info2->buses[j].currentY;
        }
      }

      String route(bus->data.route);
      String terminal(bus->data.terminal);
      String name = route + terminal;
      int16_t x1, y1;
      uint16_t w, h;
      matrix.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);
      info->longestNamePx = max(info->longestNamePx, w);
    }
  }
};

bool reading;
uint8_t readBuf[DATA_SIZE];
uint16_t readIndex;

#ifdef IS_FIRST_DISPLAY
// Group trip colors by stops that could have the same bus
TripColors tripColors_40and46;
TripColors tripColors;

BusSchedule stop(0, 39, "40 & Lyn N", &tripColors);
BusSchedule stop1(39, 84 - 39, "40 & Lyn S", &tripColors_40and46);
BusSchedule stop2(84, 128 - 84, "46 & Lyn S", &tripColors_40and46);

#else
TripColors tripColors_113;
BusSchedule stop(0, 64, "40 & Grand N", &tripColors_113);
#endif

void setup() {
  Serial.begin(SERIAL_BAUD);
  SerialESP32.begin(SERIAL_BAUD);

  pinMode(LED_BUILTIN, OUTPUT);
  
  // Start the ESP32
  pinMode(ESP32_GPIO0, OUTPUT);
  digitalWrite(ESP32_GPIO0, HIGH); // Not uploading
  delay(100);
  pinMode(ESP32_RESETN, OUTPUT);
  digitalWrite(ESP32_RESETN, HIGH); // Enable ESP32

  // Initialize matrix
  ProtomatterStatus matrixStatus = matrix.begin();
  while (matrixStatus != PROTOMATTER_OK) {
    Serial.println("Protomatter begin failed");
    delay(1000);
  }
  matrix.setFont(&Picopixel);
}

void readIncomingData() {
  // Limit number of bytes read to keep reasonable frame rate
  for (int i = 0; i < SERIAL_BYTES_PER_LOOP; i++) {
    if (!SerialESP32.available())
      break;

    uint8_t c = SerialESP32.read();

    if (reading) {
      readBuf[readIndex++] = c;
      if (readIndex == DATA_SIZE) {
        // Got all data
        reading = false;

        ScheduleData *data = (ScheduleData*) readBuf;
#ifdef IS_FIRST_DISPLAY
        if (data->stopIndex == 0) {
          stop1.handleResponse(data);
        } else if (data->stopIndex == 1) {
          stop2.handleResponse(data);
        } else if (data->stopIndex == 2) {
          stop3.handleResponse(data);
        }
#else
        // Only one stop to handle
        stop.handleResponse(data);
#endif
      }
    } else {
      // Wait for start to ensure we're reading the right data
      if (c == START_BYTE) {
        reading = true;
        readIndex = 0;
      }
    }
  }
}

void loop() {
  long start = millis();
  readIncomingData();

  matrix.fillScreen(0);
#ifdef IS_FIRST_DISPLAY
  stop1.draw();
  stop2.draw();
  stop3.draw();
#else
  stop.draw();
#endif
  matrix.show();

  // Run at about 50 fps
  long diff = millis() - start;
  if (diff < 20)
    delay(20 - diff);
}
