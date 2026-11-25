#include "keyboard.h"
#include <NimBLEDevice.h>
#include <atomic>

static void (*keyCb)(uint16_t, uint8_t, bool, bool);
static const NimBLEUUID HID_SERVICE((uint16_t)0x1812), HID_REPORT((uint16_t)0x2A4D);
static NimBLEClient *client;
static NimBLEAddress target;
static std::atomic<bool> haveTarget;
static std::atomic<const char *> state{"off"};
static uint16_t held[7]; // keys down now, as modifiers << 8 | usage (usage 0 = a modifier pressed by itself)
static uint16_t prevMedia;
static char failed[64];  // why the last connection attempt gave up

static void key(uint16_t k, bool down) { keyCb(k & 0xFF, k >> 8, false, down); }

static void onReport(NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) {
  // Guesses the report kind from its length rather than parsing the HID report map (characteristic 0x2A4B).
  // Covers ordinary keyboards and media keys; extend here for anything else.
  if (n >= 7) { // keyboard: modifiers, then six key slots (with a reserved byte between them in 8-byte reports)
    const uint8_t mod = d[0], *keys = d + (n >= 8 ? 2 : 1);
    uint16_t now[7] = {};
    int m = 0;
    for (int i = 0; i < 6; i++) if (keys[i]) now[m++] = mod << 8 | keys[i];
    if (!m && mod) now[m++] = mod << 8; // macro pads send a lone Ctrl/Shift/Alt as a key of its own
    // a key stays the same key while its usage is down, even if the modifiers change; a lone modifier is its own key
    auto same = [](uint16_t a, uint16_t b) { return (a & 0xFF) ? (a & 0xFF) == (b & 0xFF) : a == b; };
    uint16_t next[7] = {};
    for (int j = 0; j < m; j++) {
      next[j] = now[j];
      for (uint16_t h : held) if (h && same(h, now[j])) next[j] = h; // released with the modifiers it was pressed with
    }
    for (uint16_t h : held) {
      bool still = false;
      for (int j = 0; j < m; j++) still |= same(h, now[j]);
      if (h && !still) key(h, false);
    }
    for (int j = 0; j < m; j++) {
      bool was = false;
      for (uint16_t h : held) was |= h && same(h, now[j]);
      if (!was) key(now[j], true);
    }
    memcpy(held, next, sizeof held);
  } else if (n == 2 || n == 3) { // consumer control: one 16-bit usage, 0 = released
    uint16_t u = d[0] | d[1] << 8;
    if (prevMedia && u != prevMedia) keyCb(prevMedia, 0, true, false);
    if (u && u != prevMedia) keyCb(u, 0, true, true);
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
    for (uint16_t &k : held) if (k) { key(k, false); k = 0; }
    if (prevMedia) keyCb(prevMedia, 0, true, false);
    prevMedia = 0;
  }
};

static bool fail(const char *step) {
  snprintf(failed, sizeof failed, "%s %s, error %d", target.toString().c_str(), step, client->getLastError());
  return false;
}
static bool attach() { // connect, encrypt (this pairs the first time) and subscribe to every input report
  if (!client->connect(target)) return fail("connect");
  if (!client->secureConnection()) return fail("pairing");
  NimBLERemoteService *svc = client->getService(HID_SERVICE);
  if (!svc) return fail("no keyboard service");
  int n = 0;
  for (auto *c : svc->getCharacteristics(true))
    if (c->getUUID() == HID_REPORT && c->canNotify() && c->subscribe(true, onReport)) n++;
  return n > 0 || fail("subscribe");
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
    if (attach()) state = "connected", failed[0] = 0;
    else client->disconnect();
    haveTarget = false;
  }
}

void keyboardBegin(void (*onKey)(uint16_t, uint8_t, bool, bool)) {
  keyCb = onKey;
  NimBLEDevice::init("holocubic");
  NimBLEDevice::setSecurityAuth(true, false, true); // bond, no passkey, LE secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  client = NimBLEDevice::createClient();
  client->setClientCallbacks(new KbdClient, true);
  // 15-30ms interval and a 4s supervision timeout: with the defaults some keyboards (an XPPen ACK05) refuse the
  // connection outright (error 574) while wifi shares the radio
  client->setConnectionParams(12, 24, 0, 400);
  client->setConnectTimeout(10000);
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
const char *keyboardError() { return failed; }
