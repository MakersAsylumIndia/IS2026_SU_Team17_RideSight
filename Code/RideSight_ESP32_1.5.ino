/*
 * RideSight — ESP32 (Arduino IDE)  ·  FINAL
 * Two WS2812B LEDs on separate pins:  right = GPIO5 (D5)   left = GPIO25 (D25)
 *
 * Functions (5): LEFT_TURN, RIGHT_TURN, LEFT_UTURN, RIGHT_UTURN, ARRIVE  (STRAIGHT = off/default)
 *
 * Turn  -> respective side LED, SOLID colour by distance (no blink):
 *        x > 400        : off
 *        400 >= x > 200 : green
 *        200 >= x > 100 : yellow
 *        100 >= x > 0   : red
 * U-turn -> respective side LED SOLID BLUE when distance < 200 m
 * Arrive -> BOTH LEDs BLINK MAGENTA when distance < 50 m
 *
 * Wire format (3 bytes, must match protocol.js):
 *   byte0 = turn (0=STRAIGHT 1=LEFT_TURN 2=RIGHT_TURN 3=LEFT_UTURN 4=RIGHT_UTURN 5=ARRIVE)
 *   byte1 = distance high byte (uint16 big-endian, metres)   byte2 = distance low byte
 *
 * Libraries: NimBLE-Arduino (h2zero, 2.x) + FastLED.  Board: match your ESP32 variant.
 * Diagnostics (default off): LED_TEST cycles LEDs ignoring BLE; SIMULATE sweeps real patterns.
 * NOTE: GPIO25 does not exist on ESP32-C3 — change PIN_LEFT if your board is a C3.
 */

#include <NimBLEDevice.h>
#include <FastLED.h>

// ───────── config ─────────
#define SERVICE_UUID   "6686e2e2-25d8-4172-818f-0886d4e8f695"   // paste EXACT from config.js
#define CHAR_CMD_UUID  "0b64cb22-701b-415d-b32c-26a5483ea35d'"
#define DEVICE_NAME    "RideSight"

#define PIN_RIGHT   5      // D5
#define PIN_LEFT    25     // D25

#define BRIGHTNESS            150     // bench-tune against visor glare
#define UTURN_DIST_M          200     // U-turn LED shows under this
#define ARRIVE_DIST_M         50      // magenta flash under this
#define HEARTBEAT_TIMEOUT_MS  3000    // no packet this long -> all off (fail-safe)

#define LED_TEST 0     // 1 = ignore BLE, cycle LEDs to verify wiring/power
#define SIMULATE 0     // 1 = sweep the real patterns with no phone

enum Turn : uint8_t { STRAIGHT = 0, LEFT_TURN, RIGHT_TURN, LEFT_UTURN, RIGHT_UTURN, ARRIVE };

// ───────── state ─────────
CRGB ledLeft[1], ledRight[1];
volatile uint8_t  g_turn   = STRAIGHT;
volatile uint16_t g_dist   = 0;
volatile uint32_t g_lastRx = 0;

// ───────── BLE ─────────
class CmdCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    NimBLEAttValue v = c->getValue();
    if (v.size() >= 3) {
      const uint8_t* p = v.data();
      g_turn   = p[0];
      g_dist   = ((uint16_t)p[1] << 8) | p[2];   // big-endian
      g_lastRx = millis();
      Serial.printf("rx turn=%u dist=%u\n", g_turn, g_dist);
    }
  }
};

static void startBLE() {
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->advertiseOnDisconnect(true);
  NimBLEService* svc = server->createService(SERVICE_UUID);
  NimBLECharacteristic* cmd = svc->createCharacteristic(
      CHAR_CMD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  cmd->setCallbacks(new CmdCallbacks());
  svc->start();

  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.addServiceUUID(SERVICE_UUID);
  NimBLEAdvertisementData scanData;
  scanData.setName(DEVICE_NAME);
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
  NimBLEDevice::startAdvertising();
}

// distance -> solid zone colour (off above 400 m)
static CRGB zoneColor(uint16_t x) {
  if (x > 400) return CRGB::Black;    // off
  if (x > 200) return CRGB::Green;    // 400..201
  if (x > 100) return CRGB::Yellow;   // 200..101
  return CRGB::Red;                   // 100..0
}

static void bootSelfTest() {
  const CRGB seq[] = { CRGB::Red, CRGB::Green, CRGB::Blue };
  for (CRGB col : seq) { ledLeft[0]=ledRight[0]=col; FastLED.show(); delay(300); }
  ledLeft[0]=ledRight[0]=CRGB::Black; FastLED.show();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("RideSight FINAL  RIGHT=GPIO%d LEFT=GPIO%d  LED_TEST=%d SIMULATE=%d\n",
                PIN_RIGHT, PIN_LEFT, LED_TEST, SIMULATE);

  FastLED.addLeds<WS2812B, PIN_RIGHT, GRB>(ledRight, 1);
  FastLED.addLeds<WS2812B, PIN_LEFT,  GRB>(ledLeft,  1);
  FastLED.setBrightness(BRIGHTNESS);
  bootSelfTest();   // if nothing shows here, it's wiring/power/pin — not BLE/website

#if !LED_TEST && !SIMULATE
  startBLE();
  Serial.println("RideSight advertising…");
#endif
}

void loop() {
  uint32_t now = millis();

#if LED_TEST
  static const CRGB test[] = { CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White };
  ledLeft[0] = ledRight[0] = test[(now / 700) % 4];
  FastLED.show(); delay(10); return;
#endif

#if SIMULATE
  struct Case { uint8_t turn; uint16_t dist; };
  static const Case cases[] = {
    {LEFT_TURN,350},{LEFT_TURN,150},{LEFT_TURN,60},    // green -> yellow -> red
    {RIGHT_TURN,150},                                  // right yellow
    {LEFT_UTURN,150},{RIGHT_UTURN,150},                // blue (<200)
    {ARRIVE,40},                                       // magenta flash (<50)
  };
  const Case& c = cases[(now / 3000) % (sizeof(cases)/sizeof(cases[0]))];
  g_turn = c.turn; g_dist = c.dist; g_lastRx = now;
#endif

  uint8_t  turn = g_turn;
  uint16_t dist = g_dist;
  ledLeft[0] = ledRight[0] = CRGB::Black;   // clear each frame

  bool haveData = (g_lastRx != 0);
  bool stale    = haveData && (now - g_lastRx > HEARTBEAT_TIMEOUT_MS);

  if (haveData && !stale) {
    switch (turn) {
      case LEFT_TURN:   ledLeft[0]  = zoneColor(dist); break;   // solid, no blink
      case RIGHT_TURN:  ledRight[0] = zoneColor(dist); break;
      case LEFT_UTURN:  if (dist < UTURN_DIST_M) ledLeft[0]  = CRGB::Blue; break;
      case RIGHT_UTURN: if (dist < UTURN_DIST_M) ledRight[0] = CRGB::Blue; break;
      case ARRIVE:
        if (dist < ARRIVE_DIST_M && ((now / 250) % 2))         // ~2 Hz flash
          ledLeft[0] = ledRight[0] = CRGB::Magenta;
        break;
      case STRAIGHT:
      default: break;
    }
  }

  FastLED.show();
  delay(10);
}
