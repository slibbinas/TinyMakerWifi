# TinyMakerWifi

Modified and extended firmware for the open-source **TinyMaker** MSLA resin 3D printer. The main additions: **WiFi connectivity** and **direct model upload from PrusaSlicer** — no more SD card shuffling.

## Features

* **WiFi setup via captive portal** — no credentials in code, configured from your phone on first boot
* **Direct upload from PrusaSlicer** ("Send to printer" button) — the printer emulates a minimal OctoPrint API
* Automatic unpacking of uploaded `.sl1` / `.zip` files into the layer format the stock firmware expects (works with both PrusaSlicer and UVtools numbering)
* New **System** menu on the printer: WiFi Info (SSID, signal, IP, reset), firmware Update info, About
* WiFi status indicator (green/grey dot) on the main menu
* Firmware update over WiFi: browser page at `/update` + PlatformIO OTA for developers *(since v1.0.2-vs-wifi-0.4)*
* Everything is optional: build switches let you compile the original, network-free firmware from the same code base

## Hardware

Stock TinyMaker electronics — **ESP32-WROOM-32E-N4** (4 MB flash, no PSRAM). No hardware modifications required; WiFi is already on the module.

## First WiFi setup

1. Power on the printer. On first boot it starts a **`TinyMaker-Setup`** access point.
2. Connect to it with your phone — a captive portal opens automatically (or browse to `http://192.168.4.1`).
3. Select your home WiFi network and enter the password.
4. The printer connects and briefly shows its IP address; credentials are stored, so next boots connect automatically (~5 s). If the saved network is unreachable, the printer simply boots in offline mode after 15 s — printing from SD works as always.

WiFi status, signal strength and IP are always visible under **System → WiFi Info**, where you can also reset the stored credentials.

## PrusaSlicer setup

1. Import the TinyMaker printer profile (`TinyMaker.ini`, in this repo) via *File → Import → Import Config*.
2. Add a **physical printer**: click the cog icon next to the printer profile → *Add physical printer*:
   * **Name:** anything (e.g. `TinyMaker WiFi`)
   * **Hostname, IP or URL:** `tinymaker.local` (or the printer's IP shown in System → WiFi Info)
   * **API Key:** any text (not verified)
   * Note: there is no "Host Type" dropdown for SL1-derived profiles — that's normal, it is fixed in the profile.
3. Click **Test** — it should report a successful connection (printer must be on and connected to WiFi).
4. Slice and press **Send to printer**. The printer shows *Receiving → Unpacking → Model ready*, and the model appears in the **Print** menu.

**Always slice with the 0.05 mm profile.** The firmware is designed for 0.05 mm source layers (when the printer is set to 0.10 mm it skips every other image). Maximum model size: 1200 layers = 60 mm.

## Firmware updates

*(since v1.0.2-vs-wifi-0.4)*

* **From a browser:** download `firmware.bin` from [Releases](https://github.com/slibbinas/TinyMakerWifi/releases), open `http://tinymaker.local/update`, select the file, press Update. Do not power off during the update — and don't worry too much either: the dual OTA partition keeps the previous firmware if the update fails.
* **For developers:** PlatformIO OTA — select the `env:tinymaker-ota` environment and Upload goes over WiFi.

## Building from source

Requirements: [VS Code + PlatformIO](https://platformio.org/).

1. Clone this repo.
2. Unpack the four vendor-verified libraries from `Firmware/Libraries/*.zip` of the original project into the `lib/` folder: `AccelStepper` (1.64), `Arduino_GFX` (1.2.0), `PNGdec` (1.0.1), `SdFat` (1.1.2). **Do not use newer versions from the registry** — the APIs changed.
3. `pio run` — the platform (`espressif32@6.5.0`, Arduino core 2.x) and the network libraries (WiFiManager, unzipLIB) are fetched automatically. Do not upgrade to Arduino core 3.x.
4. First flash goes over USB (`env:tinymaker`, CH340 serial); after that OTA works (`env:tinymaker-ota`).

Build switches at the top of the main `.ino`:

```cpp
#define ENABLE_NETWORK       1   // 0 = original firmware behavior, no network code
#define ENABLE_SERIAL_DEBUG  1   // 0 = no serial output
```

## Credits & Acknowledgements

* **Original project:** [TinyMaker-Open-Source-3D-Printer](https://github.com/TinyMaker3D/TinyMaker-Open-Source-3D-Printer)
* **Original authors:** TinyMaker3D Team

## License

This project retains the original dual licensing of the TinyMaker3D project:

* **Firmware:** MIT License
* **Hardware:** CC BY-NC-SA 4.0

See [`LICENSE.md`](LICENSE.md) for full details and copyright notices.