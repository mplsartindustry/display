#include <Adafruit_Protomatter.h>

uint8_t rgbPins[]  = { 7, 8, 9, 10, 11, 12 };
uint8_t addrPins[] = { 17, 18, 19, 20 };
uint8_t clockPin   = 14;
uint8_t latchPin   = 15;
uint8_t oePin      = 16;

Adafruit_Protomatter matrix(
  64, 4, 1, rgbPins, 4, addrPins, clockPin, latchPin, oePin, false);

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

void setup(void) {
  // initialize matrix
  ProtomatterStatus status = matrix.begin();

  // wait for matrix to be ready
  if (status != PROTOMATTER_OK) {
    for(;;);
  }
}

void wave(int wave) {
  for (int x = 0; x < matrix.width(); x++) {
    for (int y = 0; y < matrix.height(); y++) {
      if (x > (wave - 1)) {
        matrix.drawPixel(x - wave, y, off);
      }
      matrix.drawPixel(x, y, map_seq(x, 0, matrix.width(), blues, 11));
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
  delay(4000);
  wave(80);
}