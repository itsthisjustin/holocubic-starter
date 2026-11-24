# HoloCubic starter

A minimal custom firmware for the ClocTeck HoloCubic: the display, the backlight, the accelerometer, the speaker and wifi, in one ~120-line Arduino sketch. Use it as the starting point for your own firmware.

The demo chimes at power-up, prints the raw tilt and the wifi state and rolls a dot around the screen as you tip the cube.

## Read this before flashing

- **It replaces the stock firmware, and there is no known way back.** ClocTeck has not published a recovery image. Flashing this overwrites the stock application for good.
- **The cube ships with ESP32 flash encryption enabled.** A normal `esptool` or PlatformIO upload writes plaintext, and the cube then fails to boot with `invalid header`. Always flash through `flash.py`, which asks the chip to encrypt what it writes. `pio run -t upload` is already wired to it.
- **Tested on one unit.** That unit's bootloader had already been replaced with a plain ESP-IDF one built for an encrypted chip (see below). Booting this firmware from the factory bootloader is untested.

If a wrong upload has already left your cube printing `invalid header`, the chip is still fine: the ROM USB loader works, and `flash.py` can write a bootable app.

## Hardware

ESP32-S3R8 (8MB octal PSRAM), 8MB flash, USB-Serial-JTAG on the USB port.

| What | GPIO | Notes |
| --- | --- | --- |
| LCD data | 15 | 320 × 240 IPS, ST7789-compatible, 4-wire SPI |
| LCD clock | 16 | |
| LCD chip select | 14 | |
| LCD D/C | 18 | |
| LCD reset | 17 | active low |
| LCD backlight | 13 | active low; PWM it for brightness |
| Accelerometer SDA / SCL | 4 / 5 | QMI8658 at I2C address 0x6B |
| Speaker bit clock / word select / data | 41 / 45 / 48 | I2S amplifier; 16-bit PCM |
| Octal PSRAM | 33–37 | not available for anything else |

The panel is write-only as wired: it never answers ID reads. It is viewed in a mirror, so the firmware flips the picture top-bottom (`LCD_MADCTL` in `src/main.cpp`).

Not mapped yet: the front RGB light (GPIO8 has some effect on it), the microSD socket (the stock firmware used the SDMMC host) and the button on the board. No microphone has been found. Pull requests welcome.

## Build and flash

Needs [PlatformIO](https://platformio.org/install/cli).

```sh
pio run                                   # build
pio run -t upload                         # flash, encrypted, through flash.py
```

The first time, wipe the settings partition as well, since that part of the flash holds leftover data:

```sh
~/.platformio/penv/bin/python flash.py .pio/build/holocubic/firmware.bin --erase-settings
```

`flash.py` picks the cube's USB port by itself when it is the only ESP32 plugged in (otherwise pass `--port`), and refuses any chip that doesn't have flash encryption on.

For wifi, copy `src/secrets.example.h` to `src/secrets.h` and fill in a 2.4GHz network.

## Flash layout

| Offset | Contents |
| --- | --- |
| 0x0 | bootloader (not touched by `flash.py`) |
| 0x8000 | partition table |
| 0x10000 | app, up to 5MB |
| 0x510000 | `nvs` settings, 64KB |

## The bootloader on the tested unit

A minimal ESP-IDF project built with these options and written to 0x0 with `esptool write-flash --encrypt`:

```
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=y
CONFIG_SECURE_FLASH_REQUIRE_ALREADY_ENABLED=y
```

This is not affiliated with ClocTeck.
