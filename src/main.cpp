// HoloCubic starter: the display, the backlight, the accelerometer and wifi, and nothing else.
// The demo prints the tilt and wifi state and rolls a dot around the screen as you tip the cube.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <Wire.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// ---- hardware ---------------------------------------------------------------
enum { W = 320, H = 240,
       LCD_MOSI = 15, LCD_SCK = 16, LCD_CS = 14, LCD_DC = 18, LCD_RST = 17,
       LCD_BL = 13,                     // backlight, active low
       IMU_SDA = 4, IMU_SCL = 5, IMU_ADDR = 0x6B }; // QMI8658
// The panel is viewed in a mirror, so the picture is flipped top-bottom.
// Bit 0x80 flips top-bottom, 0x40 left-right; 0x60 is plain landscape.
static const uint8_t LCD_MADCTL = 0xE0;

static Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED /* no MISO: the panel is write-only */);
static Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 1 /* landscape */, true /* IPS */, H, W);

static void displayBegin() {
  gfx->begin(40000000);
  bus->beginWrite();
  bus->writeC8D8(0x36, LCD_MADCTL);
  bus->endWrite();
  gfx->fillScreen(RGB565_BLACK);
  ledcAttach(LCD_BL, 5000, 8);
}

static void backlight(uint8_t level) { ledcWrite(LCD_BL, 255 - level); } // 0 off .. 255 full

static void imuBegin() {
  Wire.begin(IMU_SDA, IMU_SCL, 400000);
  const uint8_t init[][2] = {{0x02, 0x40} /* register auto-increment */, {0x03, 0x15} /* +-4g */, {0x08, 0x01} /* accelerometer on */};
  for (auto &r : init) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(r[0]);
    Wire.write(r[1]);
    Wire.endTransmission();
  }
}

// Raw acceleration, 8192 per g. Lying flat, z reads about +8192.
static bool imuRead(int16_t v[3]) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x35);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)IMU_ADDR, 6) != 6) return false;
  for (int i = 0; i < 3; i++) { uint8_t lo = Wire.read(); v[i] = (int16_t)(lo | Wire.read() << 8); }
  return true;
}

// ---- demo -------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  displayBegin();
  backlight(255);
  imuBegin();
#ifdef WIFI_SSID
  WiFi.begin(WIFI_SSID, WIFI_PASS);
#endif
  gfx->setTextSize(2);
}

void loop() {
  static int dotX = W / 2, dotY = H / 2;
  static int16_t rest[3];
  static bool haveRest;
  int16_t a[3] = {0, 0, 0};
  bool ok = imuRead(a);
  // The sensor sits at an angle inside the case, so "level" is whatever it read first.
  if (ok && !haveRest) { memcpy(rest, a, sizeof rest); haveRest = true; }

  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK); // a background colour overwrites the old text without flicker
  gfx->setCursor(8, 8);
  gfx->printf("tilt %6d %6d %6d ", a[0], a[1], a[2]);
  gfx->setCursor(8, 32);
#ifdef WIFI_SSID
  if (WiFi.status() == WL_CONNECTED) gfx->printf("wifi %s   ", WiFi.localIP().toString().c_str());
  else gfx->print("wifi connecting...   ");
#else
  gfx->print("wifi: add src/secrets.h");
#endif

  gfx->fillCircle(dotX, dotY, 8, RGB565_BLACK);
  dotX = constrain(W / 2 + (a[0] - rest[0]) * (W / 2) / 3000, 8, W - 9); // full travel at about 20 degrees
  dotY = constrain(H / 2 + 30 + (a[1] - rest[1]) * (H / 2) / 3000, 64, H - 9);
  gfx->fillCircle(dotX, dotY, 8, ok ? RGB565_ORANGE : RGB565_RED);

  static uint32_t last;
  if (millis() - last > 1000) { last = millis(); Serial.printf("accel %d %d %d imu=%d wifi=%d\n", a[0], a[1], a[2], ok, (int)WiFi.status()); }
  delay(30);
}
