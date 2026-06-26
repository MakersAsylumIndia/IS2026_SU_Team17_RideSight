/*
 * RideSight — ESP32 (Arduino IDE)
 * Three WS2812B LEDs on SEPARATE data pins:
 *   left = GPIO5 (D5), centre = GPIO18 (D18), right = GPIO19 (D19)
 *
 * LED behaviour (this version):
 *   RIGHT_TURN  -> fade the RIGHT LED  (breathing fade)
 *   LEFT_TURN   -> fade the LEFT  LED  (breathing fade)
 *   LEFT_UTURN  -> LEFT  LED solid BLUE when distance < 500 m (else off)
 *   RIGHT_UTURN -> RIGHT LED solid BLUE when distance < 500 m (else off)
 *   STRAIGHT    -> nothing (centre LED has no assigned pattern yet)
 *
 * turn + distance arrive over BLE from the phone (3-byte payload, must match
 * the web app's protocol.js):
 *   byte 0 = turn (0=STRAIGHT 1=LEFT_TURN 2=RIGHT_TURN 3=LEFT_UTURN 4=RIGHT_UTURN)
 *   byte 1 = distance high byte (uint16, big-endian, metres)
 *   byte 2 = distance low  byte
 *
 * Libraries (Arduino IDE -> Library Manager):
 *   - "NimBLE-Arduino" by h2zero   (this code needs 2.x)
 *   - "FastLED"
 * Board: "ESP32 Dev Module" (Espressif esp32 core).
 */

#include <NimBLEDevice.h>
#include <FastLED.h>

// ───────── config ─────────
// Paste the EXACT UUIDs from your web app's src/config.js (prefixes are hints).
#define SERVICE_UUID   "6686e2e2-25d8-4172-818f-0886d4e8f695"
#define CHAR_CMD_UUID  "0b64cb22-701b-415d-b32c-26a5483ea35d"
#define DEVICE_NAME    "RideSight"

#define PIN_LEFT    5     // D5
#define PIN_CENTRE  18    // D18
#define PIN_RIGHT   19    // D19

#define BRIGHTNESS        100  // global ceiling — BENCH-TUNE against visor glare
#define FADE_BPM          30    // breathing fade speed (breaths/min) — tunable
#define TURN_HUE          32    // fade colour for turns (FastLED HSV: ~32 = amber). Blue is reserved for U-turns.
#define UTURN_DIST_M      500   // U-turn LED shows only under this distance
#define HEARTBEAT_TIMEOUT_MS 3000  // no packet this long -> LEDs off (fail-safe)

#define SIMULATE 0       // set to 1 to cycle the patterns WITHOUT a phone (bench test)

enum Turn : uint8_t { STRAIGHT = 0, LEFT_TURN, RIGHT_TURN, LEFT_UTURN, RIGHT_UTURN };

// ───────── state ─────────
CRGB ledLeft[1], ledCentre[1], ledRight[1];
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
    }
  }
};

static void startBLE() {
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->advertiseOnDisconnect(true);           // 2.x: auto re-advertise is OFF by default

  NimBLEService* svc = server->createService(SERVICE_UUID);
  NimBLECharacteristic* cmd = svc->createCharacteristic(
      CHAR_CMD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  cmd->setCallbacks(new CmdCallbacks());
  svc->start();

  // Service UUID in the MAIN advertisement packet (Web Bluetooth / Bluefy filter
  // on it); device NAME in the scan response (both won't fit in 31 bytes).
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.addServiceUUID(SERVICE_UUID);
  NimBLEAdvertisementData scanData;
  scanData.setName(DEVICE_NAME);

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);                 // 2.x: must enable explicitly
  NimBLEDevice::startAdvertising();
}

// ───────── boot self-test: R, G, B on each LED so you can verify wiring/colour ─────────
static void bootSelfTest() {
  const CRGB seq[] = { CRGB::Red, CRGB::Green, CRGB::Blue };
  for (CRGB col : seq) {
    ledLeft[0] = ledCentre[0] = ledRight[0] = col;
    FastLED.show();
    delay(250);
  }
  ledLeft[0] = ledCentre[0] = ledRight[0] = CRGB::Black;
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, PIN_LEFT,   GRB>(ledLeft,   1);
  FastLED.addLeds<WS2812B, PIN_CENTRE, GRB>(ledCentre, 1);
  FastLED.addLeds<WS2812B, PIN_RIGHT,  GRB>(ledRight,  1);
  FastLED.setBrightness(BRIGHTNESS);
  bootSelfTest();
#if !SIMULATE
  startBLE();
  Serial.println("RideSight advertising…");
#else
  Serial.println("RideSight SIMULATE mode");
#endif
}

void loop() {
  uint32_t now = millis();

#if SIMULATE
  // cycle the four maneuvers every 4 s; distance fixed under 500 m so U-turns show
  const uint8_t cyc[] = { LEFT_TURN, RIGHT_TURN, LEFT_UTURN, RIGHT_UTURN };
  g_turn   = cyc[(now / 4000) % 4];
  g_dist   = 300;
  g_lastRx = now;
#endif

  uint8_t  turn = g_turn;
  uint16_t dist = g_dist;

  ledLeft[0] = ledCentre[0] = ledRight[0] = CRGB::Black;   // clear each frame

  bool haveData = (g_lastRx != 0);
  bool stale    = haveData && (now - g_lastRx > HEARTBEAT_TIMEOUT_MS);

  if (haveData && !stale) {
    uint8_t fade = beatsin8(FADE_BPM, 0, 255);   // breathing brightness 0..255
    switch (turn) {
      case LEFT_TURN:   ledLeft[0]  = CHSV(TURN_HUE, 255, fade); break;
      case RIGHT_TURN:  ledRight[0] = CHSV(TURN_HUE, 255, fade); break;
      case LEFT_UTURN:  if (dist < UTURN_DIST_M) ledLeft[0]  = CRGB::Blue; break;
      case RIGHT_UTURN: if (dist < UTURN_DIST_M) ledRight[0] = CRGB::Blue; break;
      case STRAIGHT:
      default: break;   // centre (D18) has no assigned pattern yet
    }
  }

  FastLED.show();
  delay(10);   // ~100 Hz; fade timing is millis-based, not delay-based
}
