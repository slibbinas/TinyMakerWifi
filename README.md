# TinyMakerWifi

Modified and extended firmware for the open-source **TinyMaker** MSLA resin 3D printer. The main additions: **WiFi connectivity**, **OTA updates**, and **direct model upload from PrusaSlicer** — no more SD card shuffling.

![TinyMaker palm-sized resin 3D printer](Images/Palm_Sized.jpg)

## Features

* **WiFi setup via captive portal** — no credentials in code, configured from your phone on first boot
* **Direct upload from PrusaSlicer** ("Send to printer" button) — the printer emulates the Prusa SL1 network protocol
* Automatic unpacking of uploaded `.sl1` / `.zip` files into the layer format the stock firmware expects (works with both PrusaSlicer and UVtools numbering)
* New **System** menu on the printer: WiFi Info (SSID, signal, IP, reset), firmware Update info, About
* WiFi status indicator (green/grey dot) on the main menu
* **Model deletion from the printer** — long-press OK on a model in the Print menu *(since v1.0.2-wifi-0.5)*
* **WiFi reset** — from the System menu, or by holding the BACK button while powering on *(since v1.0.2-wifi-0.5)*
* **OTA updates** — firmware update over WiFi: browser page at `/update` + PlatformIO OTA for developers *(since v1.0.2-wifi-0.4)*
* Everything is optional: build switches let you compile the original, network-free firmware from the same code base

## Hardware

Stock TinyMaker electronics — **ESP32-WROOM-32E-N4** (4 MB flash, no PSRAM). No hardware modifications required; WiFi is already on the module.

## Initial Firmware Installation

If you are installing this firmware for the first time, you need to flash it via USB. After this one-time step, all future updates can be done wirelessly via your browser.

### 1. Install Drivers (if needed)
Modern browsers and Windows usually detect the CH340 USB chip automatically. If your computer does **not** recognize the printer when connected via USB, install the CH340 driver from the `Driver` folder of this repository:
* Run `CH341SER.EXE`.

### 2. Download the firmware
Download the latest **`firmware-full.bin`** from the [Releases](https://github.com/slibbinas/TinyMakerWifi/releases) section of this repository.

> ⚠️ **Which file do I need?** Releases contain two files and they are NOT interchangeable:
>
> | File | Used for | How |
> |---|---|---|
> | **`firmware-full.bin`** | **First-time USB flashing** (this section) | Flash Download Tool, address **`0x0`** |
> | `firmware.bin` | Wireless updates **only**, after this firmware is already installed | Browser, `http://tinymaker.local/update` |
>
> Flashing `firmware.bin` over USB will **not** work correctly: it lacks the bootloader and partition table, so the printer either won't boot (if flashed at `0x0`) or OTA updates will be broken (if flashed at `0x10000` over the stock firmware).

### 3. Flash it — Option A: web tool (recommended, no install)

The easiest way — works straight from a Chrome/Edge browser, no drivers or software to install *(thanks to the community for the tip)*:

1. Open **[https://esptool.spacehuhn.com/](https://esptool.spacehuhn.com/)** in Chrome or Edge.
2. Click **Connect** and select your printer's serial port (if unsure which one it is, unplug and replug the USB cable and watch which entry appears).
3. **Remove all pre-filled entries** in the list.
4. Click **ADD**, upload your **`firmware-full.bin`**, and make sure its address field is set to **`0`**.
5. Click **Program**, wait for it to finish, and power cycle the printer.

### 3. Flash it — Option B: Espressif Flash Download Tool (Windows)

If you prefer the official desktop tool:

1. Get **`flash_download_tool.zip`** from the `Flash_Installer` folder of this repo (or the official [Espressif Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html) page). **Extract the ZIP fully before running.**
2. Run the extracted `flash_download_tool_xxx.exe`.
3. In the "Download Tool" window, select **ESP32** and **Develop** mode.
4. Configure the settings **exactly** as follows (wrong settings are the most common cause of a non-booting printer):
    * **SPI Speed:** 40 MHz
    * **SPI Mode:** DIO
    * **Flash Size:** 32 Mbit (4MB)
5. Click the three dots `...` next to the first row and select **`firmware-full.bin`** (not `firmware.bin`!).
6. In the address field next to the file, enter **`0x0`** (zero — not `0x10000`).
7. Ensure the checkbox on the left of the file path is **checked** — without it the tool flashes nothing and still reports success.
8. Select the correct **COM port**, click **START**, and power cycle the printer when it says "FINISH".

Note: the first boot after flashing may take a few seconds longer than usual, and the printer will start the `TinyMaker-Setup` WiFi access point (see below). Printer settings (exposure, layer height, etc.) reset to factory defaults.

## First WiFi setup

1. Power on the printer. On first boot it starts a **`TinyMaker-Setup`** access point.
2. Connect to it with your phone — a captive portal opens automatically (or browse to `http://192.168.4.1`).
3. Select your home WiFi network and enter the password.
4. The printer connects and briefly shows its IP address; credentials are stored, so next boots connect automatically (~5 s). If the saved network is unreachable, the printer simply boots in offline mode after 15 s — printing from SD works as always.

WiFi status, signal strength and IP are always visible under **System → WiFi Info**.

### Resetting WiFi

Two ways to erase the stored credentials (e.g. when moving the printer to another network):

* **From the menu:** System → WiFi Info → press OK → confirm. The printer erases the credentials, reboots and starts the `TinyMaker-Setup` portal again.
* **Emergency reset:** hold the **BACK** button while powering the printer on. Use this if the printer keeps trying to connect to an old network and you can't reach the menu in time.

## PrusaSlicer setup

1. Import the TinyMaker printer profile (`TinyMaker.ini`, in this repo) via *File → Import → Import Config*.
2. Add a **physical printer**: click the cog icon next to the printer profile → *Add physical printer*:
   * **Name:** anything (e.g. `TinyMaker WiFi`)
   * **Hostname, IP or URL:** `tinymaker.local` (or the printer's IP shown in System → WiFi Info)
   * **API Key:** any text (not verified)
   * Note: The printer emulates the Prusa SL1 network protocol.
3. Click **Test** — it should report a successful connection (printer must be on and connected to WiFi).
4. Slice and press **Send to printer**. The printer shows *Receiving → Unpacking → Model ready*, and the model appears in the **Print** menu.

**Always slice with the 0.05 mm profile.** The firmware is designed for 0.05 mm source layers (when the printer is set to 0.10 mm it skips every other image). Maximum model size: 1200 layers = 60 mm.

## Deleting uploaded models

In the **Print** menu, **press and hold OK for ~2.5 seconds** on a model — a *Delete model?* confirmation appears (release the button first, then OK = delete, Back = cancel). Deletion removes the whole model folder from the SD card and shows a progress bar (large models take a while — hundreds of layer files). A short OK press starts printing as usual.

## Wireless Firmware Updates

*(since v1.0.2-wifi-0.4)*

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

## Support this project

If you find this project useful and want to support my work, you can [buy me a coffee via PayPal](https://paypal.me/Sidlauskas?locale.x=en_US&country.x=LT).

## Credits & Acknowledgements

* **Original project:** [TinyMaker-Open-Source-3D-Printer](https://github.com/TinyMaker3D/TinyMaker-Open-Source-3D-Printer)
* **Original authors:** TinyMaker3D Team

## License

This project retains the original dual licensing of the TinyMaker3D project:

* **Firmware:** MIT License
* **Hardware:** CC BY-NC-SA 4.0

See `LICENSE.md` for full details and copyright notices.