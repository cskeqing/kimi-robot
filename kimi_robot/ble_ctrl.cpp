#include "config.h"
#include "ble_ctrl.h"
#include <BLEUtils.h>
#include <BLE2902.h>

static BLEUUID SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static BLEUUID CHAR_RX_UUID ("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
static BLEUUID CHAR_TX_UUID ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

BLEServer*         bleServer = nullptr;
BLECharacteristic* txChar    = nullptr;
static bool        bleConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConnected = true;  Serial.println("[BLE] Client connected"); }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    Serial.println("[BLE] Client disconnected");
    s->getAdvertising()->start();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String s = c->getValue();
    if (s.length() == 0) return;
    s.trim();
    Serial.printf("[BLE RX] %s\n", s.c_str());
    if (cmdQueue) {
      CmdMsg m;
      strlcpy(m.text, s.c_str(), sizeof(m.text));
      xQueueSend(cmdQueue, &m, 0);
    }
  }
};

void bleInit() {
  BLEDevice::init("Kimi-Robot");
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  BLEService* svc = bleServer->createService(SERVICE_UUID);

  BLECharacteristic* rx = svc->createCharacteristic(
      CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new RxCallbacks());

  txChar = svc->createCharacteristic(
      CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  svc->start();
  BLEAdvertising* adv = bleServer->getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
  Serial.println("[BLE] Advertising as 'Kimi-Robot' (UART mode)");
}

bool bleIsConnected() { return bleConnected; }
