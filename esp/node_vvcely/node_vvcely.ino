#include <SPI.h>
#include <LoRa.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SS   10
#define RST  17
#define DIO0 4

#define SERVICE_UUID           "6E400001-B5B4-4455-8A97-B5F3C4E4B9DC"
#define CHARACTERISTIC_UUID_RX "6E400002-B5B4-4455-8A97-B5F3C4E4B9DC"
#define CHARACTERISTIC_UUID_TX "6E400003-B5B4-4455-8A97-B5F3C4E4B9DC"

BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
String loraQueue = "";
bool pendingLoRa = false;
int msgCounter = 0;

void sendBLE(String msg) {
  Serial.println(msg);
  if (deviceConnected) {
    pTxCharacteristic->setValue((msg + "\n").c_str());
    pTxCharacteristic->notify();
    delay(50);
  }
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    delay(500);
    sendBLE("[System] Pripojeny");
  }
  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    BLEDevice::getAdvertising()->start();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();
    if (value.length() > 0) {
      msgCounter++;
      loraQueue = "MSG:" + String(msgCounter) + ":" + value;
      pendingLoRa = true;
      sendBLE("[Odoslane #" + String(msgCounter) + "] " + value);
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(12, 13, 11, 10);
  LoRa.setPins(SS, RST, DIO0);
  while (!LoRa.begin(868E6)) {
    Serial.println("Hladam LoRa...");
    delay(500);
  }
  LoRa.setSyncWord(0x34);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  LoRa.enableCrc();
  LoRa.setTxPower(20);
  Serial.println("LoRa OK!");

  BLEDevice::init("Node vcely");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new RxCallbacks());
  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("BLE OK!");
}

void loop() {
  // Prijmi zo Serial pre testovanie
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      msgCounter++;
      loraQueue = "MSG:" + String(msgCounter) + ":" + input;
      pendingLoRa = true;
      sendBLE("[Odoslane #" + String(msgCounter) + "] " + input);
    }
  }

  // Odosli z fronty
  if (pendingLoRa) {
    pendingLoRa = false;
    sendBLE("[LoRa] Posielam: " + loraQueue);
    LoRa.beginPacket();
    LoRa.print(loraQueue);
    int result = LoRa.endPacket();
    sendBLE("[LoRa] endPacket: " + String(result));
    loraQueue = "";
  }

  // Prijmi LoRa
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String received = "";
    while (LoRa.available()) {
      received += (char)LoRa.read();
    }

    static unsigned long lastTime = 0;
    unsigned long now = millis();
    if (now - lastTime > 2000) {
      lastTime = now;
      int rssi = LoRa.packetRssi();

      if (received.startsWith("ACK:")) {
        // Prijali sme ACK
        String id = received.substring(4);
        sendBLE("[ACK] Sprava #" + id + " dorucena!");
        sendBLE("[RSSI] " + String(rssi) + " dBm");

      } else if (received.startsWith("MSG:")) {
        // Prijali sme spravu, posli ACK
        int firstColon = received.indexOf(':', 4);
        String id = received.substring(4, firstColon);
        String msg = received.substring(firstColon + 1);
        sendBLE("[#" + id + "] " + msg);
        sendBLE("[RSSI] " + String(rssi) + " dBm");
        // ACK do fronty
        loraQueue = "ACK:" + id;
        pendingLoRa = true;
      }
    }
  }

  delay(10);
}