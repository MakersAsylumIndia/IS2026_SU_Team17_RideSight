/*
 * RideSight — ESP32 (Arduino IDE)  ·  2-LED build + diagnostics
 * Two WS2812B LEDs on separate pins:  right = GPIO5 (D5)   left = GPIO25 (D25)
 *
 * >>> "LEDs not lighting" debug aids <<<
 *   - boot self-test flashes both LEDs R->G->B once at startup
 *   - set LED_TEST 1 to IGNORE BLE and cycle the LEDs forever (isolate wiring from data path)
 *   - received packets are printed to Serial (115200) so you can see if data arrives
 *   NOTE: if your NEW board is an ESP32-C3, GPIO25 does NOT exist — pick a valid pin and
 *         update PIN_LEFT. On classic ESP32 / S3, GPIO5 and GPIO25 are fine.
 *
 * Behaviour (when LED_TEST 0):
 *   LEFT_TURN / RIGHT_TURN -> side LED SOLID by distance:  >500 off | 500-300 green | 300-150 yellow | 150-0 red
 *   LEFT_UTURN / RIGHT_UTURN -> side LED SOLID BLUE when distance < 500 m
 *   ARRIVE -> BOTH LEDs FLASH MAGENTA when distance < 50 m
 *   STRAIGHT -> nothing
 *
 * Wire format (3 bytes, must match protocol.js):
 *   byte0 = turn (0=STRAIGHT 1=LEFT_TURN 2=RIGHT_TURN 3=LEFT_UTURN 4=RIGHT_UTURN 5=ARRIVE)
 *   byte1 = distance high byte (uint16 big-endian, metres)   byte2 = distance low byte
 *
 * Libraries: NimBLE-Arduino (h2zero, 2.x) + FastLED.  Board: match your actual ESP32 variant.
 */

#include <NimBLEDevice.h>
#include <FastLED.h>

// ───────── config ─────────
#define SERVICE_UUID   "6686e2e2-25d8-4172-818f-0886d4e8f695"   // paste EXACT from config.js
#define CHAR_CMD_UUID  "0b64cb22-701b-415d-b32c-26a5483ea35d'"
#define DEVICE_NAME    "RideSight"

#define PIN_RIGHT   5      // D5   (verify this GPIO exists & is wired to the RIGHT LED's DIN)
#define PIN_LEFT    25     // D25  (verify; does NOT exist on ESP32-C3)

#define BRIGHTNESS            150
#define UTURN_DIST_M          500
#define ARRIVE_DIST_M         50
#define HEARTBEAT_TIMEOUT_MS  3000

#define LED_TEST 0     // 1 = ignore BLE, continuously cycle LEDs to verify wiring/power
#define SIMULATE 0     // 1 = sweep the real patterns with no phone (bench test)

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
      g_dist   = ((uint16_t)p[1] << 8) | p[2];
      g_lastRx = millis();
      Serial.printf("rx turn=%u dist=%u\n", g_turn, g_dist);   // see if packets arrive
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

static CRGB zoneColor(uint16_t x) {
  if (x > 500) return CRGB::Black;
  if (x > 300) return CRGB::Green;
  if (x > 150) return CRGB::Yellow;
  return CRGB::Red;
}

static void bootSelfTest() {
  const CRGB seq[] = { CRGB::Red, CRGB::Green, CRGB::Blue };
  for (CRGB col : seq) { ledLeft[0]=ledRight[0]=col; FastLED.show(); delay(300); }
  ledLeft[0]=ledRight[0]=CRGB::Black; FastLED.show();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("RideSight boot. RIGHT=GPIO%d  LEFT=GPIO%d  LED_TEST=%d SIMULATE=%d\n",
                PIN_RIGHT, PIN_LEFT, LED_TEST, SIMULATE);

  FastLED.addLeds<WS2812B, PIN_RIGHT, GRB>(ledRight, 1);
  FastLED.addLeds<WS2812B, PIN_LEFT,  GRB>(ledLeft,  1);
  FastLED.setBrightness(BRIGHTNESS);
  bootSelfTest();   // if you see NOTHING here, it's wiring/power/pin — not BLE/website

#if !LED_TEST && !SIMULATE
  startBLE();
  Serial.println("RideSight advertising…");
#endif
}

void loop() {
  uint32_t now = millis();

#if LED_TEST
  // Wiring isolation: cycle both LEDs through R/G/B/White, ignore BLE entirely.
  static const CRGB test[] = { CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White };
  CRGB c = test[(now / 700) % 4];
  ledLeft[0] = ledRight[0] = c;
  FastLED.show();
  delay(10);
  return;
#endif

#if SIMULATE
  struct Case { uint8_t turn; uint16_t dist; };
  static const Case cases[] = {
    {LEFT_TURN,450},{LEFT_TURN,250},{LEFT_TURN,100},
    {RIGHT_TURN,250},{LEFT_UTURN,300},{RIGHT_UTURN,300},{ARRIVE,40},
  };
  const Case& c = cases[(now / 3000) % (sizeof(cases)/sizeof(cases[0]))];
  g_turn = c.turn; g_dist = c.dist; g_lastRx = now;
#endif

  uint8_t  turn = g_turn;
  uint16_t dist = g_dist;
  ledLeft[0] = ledRight[0] = CRGB::Black;

  bool haveData = (g_lastRx != 0);
  bool stale    = haveData && (now - g_lastRx > HEARTBEAT_TIMEOUT_MS);

  if (haveData && !stale) {
    switch (turn) {
      case LEFT_TURN:   ledLeft[0]  = zoneColor(dist); break;
      case RIGHT_TURN:  ledRight[0] = zoneColor(dist); break;
      case LEFT_UTURN:  if (dist < UTURN_DIST_M) ledLeft[0]  = CRGB::Blue; break;
      case RIGHT_UTURN: if (dist < UTURN_DIST_M) ledRight[0] = CRGB::Blue; break;
      case ARRIVE:
        if (dist < ARRIVE_DIST_M && ((now / 250) % 2))
          ledLeft[0] = ledRight[0] = CRGB::Magenta;
        break;
      case STRAIGHT:
      default: break;
    }
  }

  FastLED.show();
  delay(10);
}
