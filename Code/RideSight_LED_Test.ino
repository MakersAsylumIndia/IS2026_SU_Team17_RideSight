/*
 * RideSight — LED TEST sketch (no BLE, just FastLED)
 * Tests the two WS2812B LEDs:  RIGHT = GPIO5 (D5)   LEFT = GPIO25 (D25)
 *
 * Purpose: prove wiring + power + pins in isolation. If this won't light them,
 * the problem is hardware, not the website or BLE.
 *
 * Install: Arduino IDE -> Library Manager -> "FastLED".  Board: your ESP32 variant.
 * Open Serial Monitor at 115200 to follow which step is running.
 *
 * NOTE: GPIO25 does NOT exist on ESP32-C3. If your board is a C3, change PIN_LEFT
 *       to a valid GPIO. On classic ESP32 / S3, 5 and 25 are fine.
 */

#include <FastLED.h>

#define PIN_RIGHT   5      // D5
#define PIN_LEFT    25     // D25
#define BRIGHTNESS  150    // lower if too bright; raise toward 255 if too dim

CRGB ledRight[1];
CRGB ledLeft[1];

void show(const char* label, CRGB r, CRGB l) {
  Serial.println(label);
  ledRight[0] = r;
  ledLeft[0]  = l;
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nRideSight LED test  (RIGHT=GPIO5, LEFT=GPIO25)");

  FastLED.addLeds<WS2812B, PIN_RIGHT, GRB>(ledRight, 1);
  FastLED.addLeds<WS2812B, PIN_LEFT,  GRB>(ledLeft,  1);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);
}

void loop() {
  // 1) ONE LED AT A TIME — tells you each pin/wire/LED works on its own
  show("RIGHT only -> white   (left should be OFF)", CRGB::White, CRGB::Black);
  delay(1500);
  show("LEFT only  -> white   (right should be OFF)", CRGB::Black, CRGB::White);
  delay(1500);

  // 2) COLOUR CHECK — if a colour looks wrong (e.g. red shows green), it's GRB vs RGB order
  show("both -> RED",   CRGB::Red,   CRGB::Red);    delay(1000);
  show("both -> GREEN", CRGB::Green, CRGB::Green);  delay(1000);
  show("both -> BLUE",  CRGB::Blue,  CRGB::Blue);   delay(1000);

  // 3) BLINK — confirms data is updating continuously, not a stuck frame
  for (int i = 0; i < 4; i++) {
    show("both -> WHITE (blink)", CRGB::White, CRGB::White); delay(250);
    show("both -> OFF   (blink)", CRGB::Black, CRGB::Black); delay(250);
  }

  Serial.println("--- cycle complete, repeating ---\n");
}
