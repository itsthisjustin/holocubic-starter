"""Flash firmware to a HoloCubic whose flash encryption is enabled (they ship that way).

usage: flash.py path/to/firmware.bin [--port /dev/...] [--erase-settings]

Run by `pio run -t upload`. Writes partitions.bin (beside the firmware) to 0x8000 and the app to
0x10000, letting the chip encrypt both, and leaves the bootloader alone. Refuses any chip that
is not an ESP32-S3 with flash encryption on, so it can't clobber some other board you have
plugged in. --erase-settings wipes the nvs partition: use it on the first flash.
"""
import sys
from pathlib import Path

from esptool.cmds import attach_flash, erase_region, write_flash
from esptool.targets.esp32s3 import ESP32S3ROM
from serial.tools.list_ports import comports

APP, APP_END = 0x10000, 0x510000  # factory partition in partitions.csv
NVS, NVS_SIZE = 0x510000, 0x10000
ESPRESSIF_USB_JTAG = (0x303A, 0x1001)


def main():
    args = sys.argv[1:]
    port = args.pop(args.index("--port") + 1) if "--port" in args else None
    files = [a for a in args if not a.startswith("--")]
    if len(files) != 1:
        raise SystemExit(__doc__)
    app = Path(files[0]).read_bytes()
    table = (Path(files[0]).parent / "partitions.bin").read_bytes()
    if app[0] != 0xE9 or app[12] != 9:
        raise SystemExit("Not an ESP32-S3 application image.")
    if APP + len(app) > APP_END:
        raise SystemExit(f"Firmware is {len(app)} bytes; the app partition holds {APP_END - APP}.")
    if not port:
        ports = [p.device for p in comports() if (p.vid, p.pid) == ESPRESSIF_USB_JTAG]
        if len(ports) != 1:
            raise SystemExit(f"Found {len(ports)} ESP32 USB ports; plug in only the cube or pass --port.")
        port = ports[0]
    chip = ESP32S3ROM(port, 115200)
    try:
        chip.connect()
        if not chip.get_flash_encryption_enabled():
            raise SystemExit("Flash encryption is off on this chip, so it is not a stock HoloCubic. Not flashing.")
        if chip.get_encrypted_download_disabled():
            raise SystemExit("This chip does not allow encrypted downloads; it cannot be flashed over USB.")
        chip = chip.run_stub()
        attach_flash(chip)
        if "--erase-settings" in args:
            erase_region(chip, NVS, NVS_SIZE, force=True)
        write_flash(chip, [(0x8000, table), (APP, app)], flash_size="8MB", encrypt=True, no_progress=True)
        chip.hard_reset()
    finally:
        chip._port.close()
    print("Flashed. The cube is restarting.")


if __name__ == "__main__":
    main()
