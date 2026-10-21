# ESP32-S3 Deauther (Guition 4848S040)

Standalone PlatformIO firmware for the **Guition ESP32-4848S040** (ESP32-S3-N16R8, 4" 480×480 ST7701 RGB panel + GT911 touch). Isolated from the `esp32game` launcher tree so it builds on its own.

Control is on the touchscreen. There is no web UI.

## Hardware

- Board: Guition / Guition-style ESP32-4848S040
- MCU: ESP32-S3-N16R8 (16 MB flash, 8 MB PSRAM)
- Display: 480×480 ST7701 RGB
- Touch: GT911

Pin map is in `src/board.h`.

## Build

Install [PlatformIO](https://platformio.org/). From this folder:

```text
pio run -e esp32-4848s040-idf4
pio run -e esp32-4848s040-idf4 -t upload
```

Two environments, one source tree:

| Environment | Core | Notes |
|---|---|---|
| `esp32-4848s040-idf4` | Arduino 2.0.17 / IDF 4.4 | Use this build for the deauth tool |
| `esp32-4848s040` | Arduino 3.x / IDF 5.x | Newer core; BLE path works, raw 802.11 TX is filtered |

Default serial port in `platformio.ini` is `COM3`. Change it if needed.

If you flash this as a game on the `esp32game` launcher, the top-right **MENU** corner returns to the launcher partition. On a standalone flash that partition is missing and the tap does nothing.

## Disclaimer

This firmware is for networks you own or have written permission to test. Using it on other people's Wi-Fi or Bluetooth devices is illegal.

## License

MIT
