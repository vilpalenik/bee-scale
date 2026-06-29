#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <SPI.h>
#include <LoRa.h>

// ─── LoRa pins (SX1276) ──────────────────────────────────────────────────────
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_SS    10
#define LORA_RST   17
#define LORA_DIO0   4

// ─── Packet types ────────────────────────────────────────────────────────────
#define PKT_DISCOVER  0x01
#define PKT_OFFER     0x02
#define PKT_REGISTER  0x03
#define PKT_ACK       0x04
#define PKT_DATA      0x05

// ─── Config ──────────────────────────────────────────────────────────────────
#define MAX_SCALES          50
#define DEFAULT_INTERVAL_S  300   // 5 minutes per scale reading
#define LORA_BATCH_INTERVAL 600   // flush buffer every 10 minutes
#define LORA_BATCH_MAX      35    // max readings per LoRa packet

// ─── ESP-NOW packet structures ───────────────────────────────────────────────
struct __attribute__((packed)) Packet {
  uint8_t type;
};

struct __attribute__((packed)) AckPacket {
  uint8_t  type;
  uint16_t interval_s;
};

struct __attribute__((packed)) DataPacket {
  uint8_t  type;
  float    weight;
  uint16_t battery_mv;
};

// ─── LoRa batch structures ───────────────────────────────────────────────────
struct __attribute__((packed)) LoraReading {
  uint8_t  scale_idx;   // index in registeredScales
  float    weight;
  uint16_t battery_mv;
};

struct __attribute__((packed)) LoraBatch {
  uint8_t     count;
  LoraReading readings[LORA_BATCH_MAX];
};

// ─── Scale registry ───────────────────────────────────────────────────────────
struct ScaleInfo {
  uint8_t  mac[6];
  uint16_t interval_s;
};

ScaleInfo   registeredScales[MAX_SCALES];
int         scaleCount = 0;
Preferences prefs;

// ─── Reading buffer ──────────────────────────────────────────────────────────
LoraBatch     batch;
unsigned long lastLoraFlush = 0;

// ─── NVS ─────────────────────────────────────────────────────────────────────

void loadScales() {
  prefs.begin("scales", true);
  scaleCount = prefs.getInt("count", 0);
  for (int i = 0; i < scaleCount; i++) {
    char keyMac[12], keyIvl[12];
    snprintf(keyMac, sizeof(keyMac), "mac_%d", i);
    snprintf(keyIvl, sizeof(keyIvl), "ivl_%d", i);
    prefs.getBytes(keyMac, registeredScales[i].mac, 6);
    registeredScales[i].interval_s = prefs.getUShort(keyIvl, DEFAULT_INTERVAL_S);
  }
  prefs.end();
  Serial.printf("[NVS] Loaded %d scale(s)\n", scaleCount);
}

void saveScale(const uint8_t *mac, uint16_t interval) {
  memcpy(registeredScales[scaleCount].mac, mac, 6);
  registeredScales[scaleCount].interval_s = interval;

  prefs.begin("scales", false);
  prefs.putInt("count", scaleCount + 1);
  char keyMac[12], keyIvl[12];
  snprintf(keyMac, sizeof(keyMac), "mac_%d", scaleCount);
  snprintf(keyIvl, sizeof(keyIvl), "ivl_%d", scaleCount);
  prefs.putBytes(keyMac, mac, 6);
  prefs.putUShort(keyIvl, interval);
  prefs.end();

  scaleCount++;
}

int findScaleIdx(const uint8_t *mac) {
  for (int i = 0; i < scaleCount; i++)
    if (memcmp(registeredScales[i].mac, mac, 6) == 0) return i;
  return -1;
}

// ─── ESP-NOW helpers ─────────────────────────────────────────────────────────

void addPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void sendAck(const uint8_t *mac, uint16_t interval) {
  AckPacket ack = {PKT_ACK, interval};
  addPeer(mac);
  esp_now_send(mac, (uint8_t *)&ack, sizeof(ack));
}

// ─── LoRa batch flush ────────────────────────────────────────────────────────

void flushBatch() {
  if (batch.count == 0) return;

  Serial.printf("[LORA] Sending batch: %d reading(s)\n", batch.count);
  LoRa.beginPacket();
  LoRa.write((uint8_t *)&batch, 1 + batch.count * sizeof(LoraReading));
  LoRa.endPacket();

  batch.count   = 0;
  lastLoraFlush = millis();
}

void addToBatch(uint8_t scaleIdx, float weight, uint16_t battery_mv) {
  if (batch.count >= LORA_BATCH_MAX) {
    Serial.println("[LORA] Batch full, flushing early");
    flushBatch();
  }

  LoraReading &r = batch.readings[batch.count++];
  r.scale_idx  = scaleIdx;
  r.weight     = weight;
  r.battery_mv = battery_mv;

  Serial.printf("[BUFFER] %d reading(s) buffered\n", batch.count);
}

// ─── ESP-NOW callbacks ───────────────────────────────────────────────────────

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < 1) return;
  uint8_t type = data[0];

  if (type == PKT_DISCOVER) {
    Serial.printf("[DISCOVER] %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    addPeer(mac);
    Packet offer = {PKT_OFFER};
    esp_now_send(mac, (uint8_t *)&offer, sizeof(offer));
  }

  if (type == PKT_REGISTER) {
    int idx = findScaleIdx(mac);
    if (idx >= 0) {
      Serial.printf("[REGISTER] Known scale %d, resending ACK\n", idx);
      sendAck(mac, registeredScales[idx].interval_s);
      return;
    }
    if (scaleCount >= MAX_SCALES) {
      Serial.println("[REGISTER] Max scales reached");
      return;
    }
    saveScale(mac, DEFAULT_INTERVAL_S);
    Serial.printf("[REGISTER] New scale #%d saved\n", scaleCount - 1);
    sendAck(mac, DEFAULT_INTERVAL_S);
  }

  if (type == PKT_DATA && len >= (int)sizeof(DataPacket)) {
    int idx = findScaleIdx(mac);
    if (idx < 0) {
      Serial.println("[DATA] Unknown scale, ignoring");
      return;
    }
    const DataPacket *dp = (const DataPacket *)data;
    Serial.printf("[DATA] Scale %d — %.3f kg  %d mV\n", idx, dp->weight, dp->battery_mv);

    addToBatch(idx, dp->weight, dp->battery_mv);
    sendAck(mac, registeredScales[idx].interval_s);
  }
}

void onSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.printf("[SEND] %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ─── Setup / Loop ────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] Apiary master starting...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed!");
    return;
  }
  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(onSent);
  Serial.println("[ESP-NOW] OK");

  loadScales();

  batch.count   = 0;
  lastLoraFlush = millis();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  while (!LoRa.begin(868E6)) {
    Serial.println("[LORA] Searching...");
    delay(500);
  }
  LoRa.setSyncWord(0x34);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  LoRa.enableCrc();
  LoRa.setTxPower(20);
  Serial.println("[LORA] OK");

  Serial.printf("[BOOT] Ready — batch flush every %d min\n", LORA_BATCH_INTERVAL / 60);
}

void loop() {
  // flush batch every 10 minutes
  if (millis() - lastLoraFlush >= (unsigned long)LORA_BATCH_INTERVAL * 1000) {
    flushBatch();
  }
  delay(100);
}
