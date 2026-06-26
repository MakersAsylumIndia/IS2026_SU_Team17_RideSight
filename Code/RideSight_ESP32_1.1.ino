/*
 * RideSight — ESP32 (Arduino IDE)  ·  LED behaviour v2
 * Three WS2812B LEDs on separate pins: left=GPIO5(D5) centre=GPIO18(D18) right=GPIO19(D19)
 *
 * Behaviour (this version):
 *   LEFT_TURN / RIGHT_TURN -> respective side LED, SOLID colour by distance (no blink):
 *        x > 500        : off
 *        500 >= x > 300 : green
 *        300 >= x > 150 : yellow
 *        150 >= x > 0   : red
 *   LEFT_UTURN / RIGHT_UTURN -> respective side LED SOLID BLUE when distance < 500 m
 *   ARRIVE                   -> BOTH side LEDs FLASH MAGENTA when distance < 200 m
 *   STRAIGHT                 -> nothing (centre LED unused for now)
 *
 * Wire format from the phone (3 bytes, must match protocol.js):
 *   byte0 = turn  (0=STRAIGHT 1=LEFT_TURN 2=RIGHT_TURN 3=LEFT_UTURN 4=RIGHT_UTURN 5=ARRIVE)
 *   byte1 = distance high byte (uint16 big-endian, metres)   byte2 = distance low byte
 *
 * Libraries: NimBLE-Arduino (h2zero, 2.x) + FastLED.  Board: ESP32 Dev Module.
 */

#include <NimBLEDevice.h>
#include <FastLED.h>

// ───────── config ─────────
#define SERVICE_UUID   "6686e2e2-XXXX-XXXX-XXXX-XXXXXXXXXXXX"   // paste EXACT from config.js
#define CHAR_CMD_UUID  "0b64cb22-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
#define DEVICE_NAME    "RideSight"

#define PIN_LEFT    5
#define PIN_CENTRE  18
#define PIN_RIGHT   19

#define BRIGHTNESS            150     // bench-tune against visor glare
#define UTURN_DIST_M          500     // U-turn LED shows under this
#define ARRIVE_DIST_M         200     // magenta flash under this
#define HEARTBEAT_TIMEOUT_MS  3000    // no packet this long -> all off (fail-safe)

#define SIMULATE 0     // 1 = sweep the patterns with no phone (bench test)

enum Turn : uint8_t { STRAIGHT = 0, LEFT_TURN, RIGHT_TURN, LEFT_UTURN, RIGHT_UTURN, ARRIVE };

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

// distance -> solid zone colour (off above 500 m)
static CRGB zoneColor(uint16_t x) {
  if (x > 500) return CRGB::Black;    // off
  if (x > 300) return CRGB::Green;    // 500..301
  if (x > 150) return CRGB::Yellow;   // 300..151
  return CRGB::Red;                   // 150..0
}

static void bootSelfTest() {
  const CRGB seq[] = { CRGB::Red, CRGB::Green, CRGB::Blue };
  for (CRGB col : seq) { ledLeft[0]=ledCentre[0]=ledRight[0]=col; FastLED.show(); delay(250); }
  ledLeft[0]=ledCentre[0]=ledRight[0]=CRGB::Black; FastLED.show();
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
  struct Case { uint8_t turn; uint16_t dist; };
  static const Case cases[] = {
    {LEFT_TURN,450},{LEFT_TURN,250},{LEFT_TURN,100},   // green -> yellow -> red
    {RIGHT_TURN,250},                                  // right side yellow
    {LEFT_UTURN,300},{RIGHT_UTURN,300},                // blue
    {ARRIVE,150},                                      // magenta flash
  };
  const Case& c = cases[(now / 3000) % (sizeof(cases)/sizeof(cases[0]))];
  g_turn = c.turn; g_dist = c.dist; g_lastRx = now;
#endif

  uint8_t  turn = g_turn;
  uint16_t dist = g_dist;
  ledLeft[0] = ledCentre[0] = ledRight[0] = CRGB::Black;   // clear each frame

  bool haveData = (g_lastRx != 0);
  bool stale    = haveData && (now - g_lastRx > HEARTBEAT_TIMEOUT_MS);

  if (haveData && !stale) {
    switch (turn) {
      case LEFT_TURN:   ledLeft[0]  = zoneColor(dist); break;   // solid, no blink
      case RIGHT_TURN:  ledRight[0] = zoneColor(dist); break;
      case LEFT_UTURN:  if (dist < UTURN_DIST_M) ledLeft[0]  = CRGB::Blue; break;
      case RIGHT_UTURN: if (dist < UTURN_DIST_M) ledRight[0] = CRGB::Blue; break;
      case ARRIVE:
        if (dist < ARRIVE_DIST_M && ((now / 250) % 2))        // ~2 Hz flash
          ledLeft[0] = ledRight[0] = CRGB::Magenta;
        break;
      case STRAIGHT:
      default: break;   // centre (D18) unused
    }
  }

  FastLED.show();
  delay(10);
}
