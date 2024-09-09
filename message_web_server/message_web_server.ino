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
#include <WiFiWebServer.h>

#include "wifi_credentials.h"

uint8_t matrixChainWidth = 64;
uint8_t bitDepth = 6;
uint8_t matrixChains = 1;
uint8_t rgbPins[] = { 7, 8, 9, 10, 11, 12 };
uint8_t addressLines = 4;
uint8_t addrPins[] = { 17, 18, 19, 20 };
uint8_t clockPin = 14;
uint8_t latchPin = 15;
uint8_t oePin = 16;
bool doubleBuffered = true;

Adafruit_Protomatter matrix(
  matrixChainWidth,
  bitDepth,
  matrixChains,
  rgbPins,
  addressLines,
  addrPins,
  clockPin,
  latchPin,
  oePin,
  doubleBuffered
);

#define rgb(r, g, b) matrix.color565(r, g, b)

uint16_t blues[] = {
  rgb(247, 251, 255),
  rgb(222, 235, 247),
  rgb(198, 219, 239),
  rgb(158, 202, 225),
  rgb(107, 174, 214),
  rgb(66, 146, 198),
  rgb(33, 113, 181),
  rgb(8, 81, 156),
  rgb(8, 48, 107),
  rgb(6, 36, 80),
  rgb(4, 24, 53),
  rgb(2, 12, 27),
  rgb(0, 2, 5),
  rgb(0, 1, 2),
  rgb(0, 0, 1)
};

uint16_t highlighted = blues[4];
uint16_t selected = blues[5];
uint16_t pressed = blues[6];
uint16_t deselected = blues[8];
uint16_t off = rgb(0, 0, 0);

uint16_t map_seq(int x, int min, int max, uint16_t colors[], int len) {
  return colors[map(x, min, max, 0, len)];
}

WiFiServer server(80);

void setup(void) {
  // initialize matrix
  ProtomatterStatus status = matrix.begin();

  // wait for matrix to be ready
  if (status != PROTOMATTER_OK) {
    for(;;);
  }

  matrix.println("Connecting to WiFi");
  matrix.show();

  while (WiFi.status() == WL_NO_MODULE) {
    matrix.println("WiFi module failed");
    delay(1000);
  }

  int wifiStatus;
  do {
    wifiStatus = WiFi.begin(wifiSsid, wifiPass);
    delay(500);
  } while (wifiStatus != WL_CONNECTED);

  clear();  
  wave(60);

  clear();
  matrix.println(WiFi.localIP());
  matrix.show();
}

void clear() {
  for (int x = 0; x < matrix.width(); x++) {
    for (int y = 0; y < matrix.height(); y++) {
      matrix.drawPixel(x, y, off);
    }
  }
}

void wave(int wave) {
  for (int x = 0; x < matrix.width(); x++) {
    for (int y = 0; y < matrix.height(); y++) {
      if (x > (wave - 1)) {
        matrix.drawPixel(x - wave, y, off);
      }
      matrix.drawPixel(x, y, map_seq(x, 0, matrix.width(), blues, 12));
    }
    matrix.show();
    delay(40);
  }
  for (int w = wave; w > 0; w--) {
    for (int y = 0; y < matrix.height(); y++) {
      matrix.drawPixel(matrix.width() - w, y, off);
    }
    matrix.show();
    delay(40);
  }
}

void loop(void) {
  // listen for incoming clients
  WiFiClient client = server.available();

  if (client) {
    bool currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          // use \r\n instead of many println statements to speedup data send
          client.print(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"  // the connection will be closed after completion of the response
            "\r\n");
          client.print("<!DOCTYPE HTML>\r\n");
          client.print("<html>\r\n");
          client.print("<h1>Hello World</h1>\r\n");
          client.print("</html>\r\n");
          break;
        }

        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }

    // give the web browser time to receive the data
    delay(10);

    // close the connection:
    client.stop();
  }
}
