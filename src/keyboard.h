// Bluetooth LE keyboard input (HID over GATT): keyboards, macro pads, dials. Pairs with the first one it finds
// in pairing mode, then only reconnects to that one. The ESP32-S3 has no classic Bluetooth, so the keyboard must do LE.
#pragma once
#include <Arduino.h>

// `usage` is a USB HID usage ID: keyboard page (0x28 enter, 0x4F right arrow...), or the consumer page when
// `media` is true (0xE9 volume up, 0xCD play/pause...). `mods` is the HID modifier byte held with it
// (0x01 ctrl, 0x02 shift, 0x04 alt, 0x08 cmd/win; right-hand ones in the high nibble), so ctrl+z and ctrl+shift+z
// are told apart. A modifier pressed by itself comes through as usage 0. Called on the Bluetooth task: keep it short.
void keyboardBegin(void (*onKey)(uint16_t usage, uint8_t mods, bool media, bool down));
void keyboardForget();       // drop the pairing so a different keyboard can pair
const char *keyboardState(); // "pairing", "waiting", "connecting" or "connected"
const char *keyboardError(); // why the last connection attempt failed, or ""
