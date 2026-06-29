#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID           "6E400001-B5B4-4455-8A97-B5F3C4E4B9DC"
#define CHARACTERISTIC_UUID_RX "6E400002-B5B4-4455-8A97-B5F3C4E4B9DC"
#define CHARACTERISTIC_UUID_TX "6E400003-B5B4-4455-8A97-B5F3C4E4B9DC"

BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("Mobil pripojeny!");
  }
  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("Mobil odpojeny, cakam znova...");
    BLEDevice::getAdvertising()->start();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();
    if (value.length() > 0) {
      Serial.print("Prijata sprava: ");
      Serial.println(value);
    }
  }
};

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  Serial.println("Spustam BLE...");

  BLEDevice::init("ESP32-S3 Test");
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
  Serial.println("Cakam na pripojenie mobilu...");
}

void loop() {
  delay(100);
}