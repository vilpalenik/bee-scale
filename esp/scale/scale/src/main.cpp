#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <HX711.h>

// ─── Pins ────────────────────────────────────────────────────────────────────
#define HX711_DAT  0
#define HX711_CLK  1

// ─── Packet types ────────────────────────────────────────────────────────────
#define PKT_DISCOVER  0x01
#define PKT_OFFER     0x02
#define PKT_REGISTER  0x03
#define PKT_ACK       0x04
#define PKT_DATA      0x05

// ─── Timing ──────────────────────────────────────────────────────────────────
#define DISCOVER_TIMEOUT_MS  5000
#define REGISTER_TIMEOUT_MS  3000
#define ACK_TIMEOUT_MS       5000
#define RETRY_DELAY_MS       10000
#define DEFAULT_INTERVAL_S   300     // 5 minutes default

// ─── HX711 calibration ───────────────────────────────────────────────────────
// Adjust CALIBRATION_FACTOR by placing a known weight and tuning until correct
#define CALIBRATION_FACTOR  -7050.0f

// ─── Packet structures ───────────────────────────────────────────────────────
struct __attribute__((packed)) Packet {
  uint8_t type;
};

struct __attribute__((packed)) AckPacket {
  uint8_t  type;
  uint16_t interval_s;  // master tells scale how long to sleep
};

struct __attribute__((packed)) DataPacket {
  uint8_t  type;
  float    weight;
  uint16_t battery_mv;
};

// ─── RTC memory — survives deep sleep ────────────────────────────────────────
RTC_DATA_ATTR uint8_t  masterMac[6]    = {0};
RTC_DATA_ATTR bool     wasRegistered   = false;
RTC_DATA_ATTR uint16_t sleepInterval_s = DEFAULT_INTERVAL_S;
RTC_DATA_ATTR long     hx711Offset     = 0;  // tare offset

// ─── Runtime state ───────────────────────────────────────────────────────────
enum State { DISCOVERING, REGISTERING, SENDING, DONE };
State    state       = DISCOVERING;
bool     masterFound = false;
bool     dataAcked   = false;
uint16_t newInterval = DEFAULT_INTERVAL_S;

HX711   hx711;
uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─── ESP-NOW helpers ─────────────────────────────────────────────────────────

void addPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

// ─── Deep sleep ──────────────────────────────────────────────────────────────

void goToSleep(uint16_t seconds) {
  uint16_t jitter = random(0, 60);  // spread wakeups across 60s window
  uint16_t total  = seconds + jitter;
  Serial.printf("[SLEEP] %d s (+ %d s jitter)\n", seconds, jitter);
  Serial.flush();
  esp_now_deinit();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup((uint64_t)total * 1000000ULL);
  esp_deep_sleep_start();
}

// ─── HX711 ───────────────────────────────────────────────────────────────────

float readWeight() {
  hx711.begin(HX711_DAT, HX711_CLK);
  hx711.set_scale(CALIBRATION_FACTOR);

  if (hx711Offset == 0) {
    // first boot: tare with empty platform, save offset
    Serial.println("[HX711] Taring...");
    hx711.tare();
    hx711Offset = hx711.get_offset();
  } else {
    // restore saved tare — skip taring on every wakeup
    hx711.set_offset(hx711Offset);
  }

  float w = hx711.get_units(10);  // average of 10 readings
  Serial.printf("[HX711] %.3f kg\n", w);
  return w;
}

// ─── Callbacks ───────────────────────────────────────────────────────────────

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < 1) return;
  uint8_t type = data[0];

  if (type == PKT_OFFER && state == DISCOVERING) {
    Serial.printf("[OFFER] %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    memcpy(masterMac, mac, 6);
    masterFound = true;
  }

  if (type == PKT_ACK) {
    if (len >= (int)sizeof(AckPacket)) {
      const AckPacket *ack = (const AckPacket *)data;
      newInterval    = ack->interval_s;
      sleepInterval_s = newInterval;
      Serial.printf("[ACK] Interval set to %d s\n", newInterval);
    }
    if (state == REGISTERING) {
      wasRegistered = true;
      state = SENDING;
    } else if (state == SENDING) {
      dataAcked = true;
      state = DONE;
    }
  }
}

void onSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.printf("[SEND] %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ─── Discovery ───────────────────────────────────────────────────────────────

void discover() {
  state       = DISCOVERING;
  masterFound = false;

  addPeer(broadcastMac);
  Packet pkt = {PKT_DISCOVER};
  esp_now_send(broadcastMac, (uint8_t *)&pkt, sizeof(pkt));
  Serial.println("[DISCOVER] Waiting 5 s for masters...");

  unsigned long start = millis();
  while (millis() - start < DISCOVER_TIMEOUT_MS) delay(10);

  if (!masterFound) {
    Serial.printf("[DISCOVER] No master found. Retry in %d s\n", RETRY_DELAY_MS / 1000);
    delay(RETRY_DELAY_MS);
    discover();
    return;
  }

  state = REGISTERING;
  addPeer(masterMac);
  Packet reg = {PKT_REGISTER};
  esp_now_send(masterMac, (uint8_t *)&reg, sizeof(reg));
  Serial.println("[REGISTER] Sent, waiting for ACK...");

  unsigned long regStart = millis();
  while (millis() - regStart < REGISTER_TIMEOUT_MS && state == REGISTERING) delay(10);

  if (state != SENDING) {
    Serial.println("[REGISTER] No ACK. Retrying discovery...");
    memset(masterMac, 0, 6);
    wasRegistered = false;
    delay(RETRY_DELAY_MS);
    discover();
  }
}

// ─── Send reading ─────────────────────────────────────────────────────────────

void sendReading() {
  state     = SENDING;
  dataAcked = false;
  newInterval = sleepInterval_s;  // default to current interval if no ACK

  DataPacket dp;
  dp.type       = PKT_DATA;
  dp.weight     = readWeight();
  dp.battery_mv = 0;  // TODO: read ADC battery voltage

  addPeer(masterMac);
  esp_now_send(masterMac, (uint8_t *)&dp, sizeof(dp));

  unsigned long start = millis();
  while (millis() - start < ACK_TIMEOUT_MS && !dataAcked) delay(10);

  if (!dataAcked) Serial.println("[DATA] No ACK, using current interval");

  goToSleep(newInterval);
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(300);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_now_init();
  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(onSent);

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_TIMER && wasRegistered) {
    Serial.printf("[BOOT] Wake from sleep. Master: %02X:%02X:%02X:%02X:%02X:%02X\n",
      masterMac[0], masterMac[1], masterMac[2],
      masterMac[3], masterMac[4], masterMac[5]);
    sendReading();
  } else {
    Serial.println("[BOOT] Cold start. Starting discovery...");
    hx711Offset = 0;  // force retare on cold boot
    discover();
    if (state == SENDING) sendReading();
  }
}

void loop() {
  // never reached — always goes to deep sleep
}
