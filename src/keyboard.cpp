#include "keyboard.h"
#include <NimBLEDevice.h>
#include <atomic>

static void (*keyCb)(uint16_t, bool, bool);
static const NimBLEUUID HID_SERVICE((uint16_t)0x1812), HID_REPORT((uint16_t)0x2A4D);
static NimBLEClient *client;
static NimBLEAddress target;
static std::atomic<bool> haveTarget;
static std::atomic<const char *> state{"off"};
static uint8_t prevKeys[6];
static uint16_t prevMedia;

static void onReport(NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) {
  // Guesses the report kind from its length rather than parsing the HID report map (characteristic 0x2A4B).
  // Covers ordinary keyboards and media keys; extend here for anything else.
  if (n >= 8) { // keyboard: modifiers, reserved, six key slots
    for (int i = 2; i < 8; i++) if (d[i] && !memchr(prevKeys, d[i], 6)) keyCb(d[i], false, true);
    for (int i = 0; i < 6; i++) if (prevKeys[i] && !memchr(d + 2, prevKeys[i], 6)) keyCb(prevKeys[i], false, false);
    memcpy(prevKeys, d + 2, 6);
  } else if (n == 2 || n == 3) { // consumer control: one 16-bit usage, 0 = released
    uint16_t u = d[0] | d[1] << 8;
    if (prevMedia && u != prevMedia) keyCb(prevMedia, true, false);
    if (u && u != prevMedia) keyCb(u, true, true);
    prevMedia = u;
  }
}

struct KbdScan : NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *d) override {
    if (haveTarget || !d->isConnectable()) return;
    // Matches a bonded keyboard by address; one that rotates private addresses would need the resolving list.
    bool ours = NimBLEDevice::getNumBonds() ? NimBLEDevice::isBonded(d->getAddress()) : d->isAdvertisingService(HID_SERVICE);
    if (!ours) return;
    target = d->getAddress();
    haveTarget = true;
    NimBLEDevice::getScan()->stop();
  }
};

struct KbdClient : NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int) override { // release anything held when the link dropped
    for (uint8_t k : prevKeys) if (k) keyCb(k, false, false);
    if (prevMedia) keyCb(prevMedia, true, false);
    memset(prevKeys, 0, sizeof prevKeys);
    prevMedia = 0;
  }
};

static bool attach() { // connect, encrypt (this pairs the first time) and subscribe to every input report
  if (!client->connect(target) || !client->secureConnection()) return false;
  NimBLERemoteService *svc = client->getService(HID_SERVICE);
  int n = 0;
  if (svc)
    for (auto *c : svc->getCharacteristics(true))
      if (c->getUUID() == HID_REPORT && c->canNotify() && c->subscribe(true, onReport)) n++;
  return n > 0;
}

static void task(void *) {
  NimBLEScan *scan = NimBLEDevice::getScan();
  for (;;) {
    if (client->isConnected()) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    if (!haveTarget) {
      state = NimBLEDevice::getNumBonds() ? "waiting" : "pairing";
      if (!scan->isScanning()) scan->start(0, false, true);
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    state = "connecting";
    if (attach()) state = "connected";
    else client->disconnect();
    haveTarget = false;
  }
}

void keyboardBegin(void (*onKey)(uint16_t, bool, bool)) {
  keyCb = onKey;
  NimBLEDevice::init("holocubic");
  NimBLEDevice::setSecurityAuth(true, false, true); // bond, no passkey, LE secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  client = NimBLEDevice::createClient();
  client->setClientCallbacks(new KbdClient, true);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new KbdScan, false);
  scan->setActiveScan(true); // some keyboards only name the HID service in their scan response
  scan->setInterval(100);
  scan->setWindow(30);       // listen 30% of the time; wifi shares the radio
  xTaskCreatePinnedToCore(task, "keyboard", 4096, NULL, 1, NULL, 0);
}

void keyboardForget() {
  if (client->isConnected()) client->disconnect();
  NimBLEDevice::deleteAllBonds();
}

const char *keyboardState() { return state; }
