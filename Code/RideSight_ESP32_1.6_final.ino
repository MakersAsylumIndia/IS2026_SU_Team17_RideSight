/*
 * RideSight — ESP32 (Arduino IDE)  ·  FINAL (Adafruit NeoPixel)
 * Two WS2812B LEDs on separate pins:  right = GPIO5 (D5)   left = GPIO25 (D25)
 * Each pin is its own NeoPixel object (NeoPixel uses one strip per data pin).
 *
 * Functions (5): LEFT_TURN, RIGHT_TURN, LEFT_UTURN, RIGHT_UTURN, ARRIVE  (STRAIGHT = off)
 *
 * Turn  -> respective side LED, SOLID colour by distance:
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
 * Libraries (Arduino IDE -> Library Manager):
 *   - "Adafruit NeoPixel"
 *   - "NimBLE-Arduino" (h2zero, 2.x)
 * Board: match your ESP32 variant.  NOTE: GPIO25 doesn't exist on ESP32-C3.
 * Diagnostics (default off): LED_TEST cycles LEDs ignoring BLE; SIMULATE sweeps real patterns.
 */

#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>

// ───────── config ─────────
#define SERVICE_UUID   "6686e2e2-25d8-4172-818f-0886d4e8f695"   // paste EXACT from config.js
#define CHAR_CMD_UUID  "0b64cb22-701b-415d-b32c-26a5483ea35d"
#define DEVICE_NAME    "RideSight"

#define PIN_RIGHT   5      // D5
#define PIN_LEFT    25     // D25

#define BRIGHTNESS            150     // bench-tune against visor glare
#define UTURN_DIST_M          200     // U-turn LED shows under this
#define ARRIVE_DIST_M         50      // magenta flash under this
#define HEARTBEAT_TIMEOUT_MS  3000    // no packet this long -> all off (fail-safe)

#define LED_TEST 0     // 1 = ignore BLE, cycle LEDs to verify wiring/power
#define SIMULATE 0     // 1 = sweep the real patterns with no phone

// packed 0xRRGGBB colours (NeoPixel reorders per NEO_GRB)
#define COL_OFF     0x000000
#define COL_RED     0xFF0000
#define COL_GREEN   0x00FF00
#define COL_BLUE    0x0000FF
#define COL_YELLOW  0xFFFF00
#define COL_MAGENTA 0xFF00FF
#define COL_WHITE   0xFFFFFF

enum Turn : uint8_t { STRAIGHT = 0, LEFT_TURN, RIGHT_TURN, LEFT_UTURN, RIGHT_UTURN, ARRIVE };

// ───────── LEDs (one NeoPixel object per pin) ─────────
Adafruit_NeoPixel right(1, PIN_RIGHT, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel left (1, PIN_LEFT,  NEO_GRB + NEO_KHZ800);

// ───────── state ─────────
volatile uint8_t  g_turn   = STRAIGHT;
volatile uint16_t g_dist   = 0;
volatile uint32_t g_lastRx = 0;

static void setLeds(uint32_t l, uint32_t r) {
  left.setPixelColor(0, l);   left.show();
  right.setPixelColor(0, r);  right.show();
}

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
static uint32_t zoneColor(uint16_t x) {
  if (x > 400) return COL_OFF;
  if (x > 200) return COL_GREEN;    // 400..201
  if (x > 100) return COL_YELLOW;   // 200..101
  return COL_RED;                   // 100..0
}

static void bootSelfTest() {
  uint32_t seq[] = { COL_RED, COL_GREEN, COL_BLUE };
  for (uint32_t col : seq) { setLeds(col, col); delay(300); }
  setLeds(COL_OFF, COL_OFF);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("RideSight FINAL (NeoPixel)  RIGHT=GPIO%d LEFT=GPIO%d  LED_TEST=%d SIMULATE=%d\n",
                PIN_RIGHT, PIN_LEFT, LED_TEST, SIMULATE);

  right.begin(); right.setBrightness(BRIGHTNESS); right.show();
  left.begin();  left.setBrightness(BRIGHTNESS);  left.show();
  bootSelfTest();   // if nothing shows here, it's wiring/power/pin — not BLE/website

#if !LED_TEST && !SIMULATE
  startBLE();
  Serial.println("RideSight advertising…");
#endif
}

void loop() {
  uint32_t now = millis();

#if LED_TEST
  uint32_t test[] = { COL_RED, COL_GREEN, COL_BLUE, COL_WHITE };
  uint32_t c = test[(now / 700) % 4];
  setLeds(c, c); delay(10); return;
#endif

#if SIMULATE
  struct Case { uint8_t turn; uint16_t dist; };
  static const Case cases[] = {
    {LEFT_TURN,350},{LEFT_TURN,150},{LEFT_TURN,60},
    {RIGHT_TURN,150},{LEFT_UTURN,150},{RIGHT_UTURN,150},{ARRIVE,40},
  };
  const Case& c = cases[(now / 3000) % (sizeof(cases)/sizeof(cases[0]))];
  g_turn = c.turn; g_dist = c.dist; g_lastRx = now;
#endif

  uint8_t  turn = g_turn;
  uint16_t dist = g_dist;
  uint32_t lColor = COL_OFF, rColor = COL_OFF;

  bool haveData = (g_lastRx != 0);
  bool stale    = haveData && (now - g_lastRx > HEARTBEAT_TIMEOUT_MS);

  if (haveData && !stale) {
    switch (turn) {
      case LEFT_TURN:   lColor = zoneColor(dist); break;   // solid, no blink
      case RIGHT_TURN:  rColor = zoneColor(dist); break;
      case LEFT_UTURN:  if (dist < UTURN_DIST_M) lColor = COL_BLUE; break;
      case RIGHT_UTURN: if (dist < UTURN_DIST_M) rColor = COL_BLUE; break;
      case ARRIVE:
        if (dist < ARRIVE_DIST_M && ((now / 250) % 2))       // ~2 Hz flash
          lColor = rColor = COL_MAGENTA;
        break;
      case STRAIGHT:
      default: break;
    }
  }

  setLeds(lColor, rColor);
  delay(10);
}
