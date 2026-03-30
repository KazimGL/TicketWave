# 🎟️ TicketWave Kiosk

A self-service ticketing kiosk built on the **ESP32-S3** with a touchscreen display, UPI payment integration, Bluetooth thermal printing, and a web-based admin portal.

---

## 📸 Overview

TicketWave is an embedded kiosk system designed for venues (parks, events, parking lots) that allows customers to:

- Browse and purchase tickets (Adult, Child, Senior)
- Book event tickets (General / VIP)
- Pay for parking
- Pay via **UPI QR code** (powered by Bonrix Payment Gateway)
- Receive a **printed receipt** via Bluetooth thermal printer

Administrators can manage pricing, configure WiFi, set a security PIN, and reboot the device — all from a browser over the local network.

---

## 🛠️ Hardware Requirements

| Component | Details |
|---|---|
| Microcontroller | ESP32-S3 |
| Display | Elecrow CrowPanel (4.3", 5.0", or **7.0"** RGB) |
| Touch | GT911 (7"/5") or XPT2046 (4.3") |
| Printer | Bluetooth BLE Thermal Printer |
| Storage | SPIFFS + NVS (on-chip flash) |
| Connectivity | WiFi (2.4GHz) + Bluetooth BLE |

---

## 📁 Project Structure

```
TicketWave/
│
├── TicketWave.ino          # Main entry point — setup() and loop()
│
├── api.cpp / api.hpp       # Bonrix UPI payment gateway integration
├── UpiData.hpp             # UpiData class (payment response model)
│
├── gv.hpp                  # Global constants and SPIFFS file paths
├── gfx_conf.h              # LovyanGFX display + touch configuration
│
├── pos_manager.cpp/.hpp    # Ticket pricing — NVS read/write
├── pin_manager.cpp/.hpp    # Admin PIN system — LVGL modal UI
│
├── printer_manager.cpp/.hpp # BLE thermal printer — scan, connect, print
│
├── wifi_helper.cpp/.h      # WiFi connect, auto-reconnect, NVS save
├── web_manager.cpp/.hpp    # HTTP admin portal (PIN management + reboot)
│
├── qrcode.c / qrcode.h     # QR code generator library (MIT)
│
└── img_*.c                 # Image assets (ticket, event, parking, donate)
```

---

## ⚙️ Display Configuration

Select your panel in `gfx_conf.h` by uncommenting the correct macro:

```cpp
#define CrowPanel_70   // 7.0 inch — 800x480
// #define CrowPanel_50   // 5.0 inch — 800x480
// #define CrowPanel_43   // 4.3 inch — 480x272
```

---

## 💳 Payment Flow

```
User selects ticket/quantity
        │
        ▼
fetch_upi_data() → POST to Bonrix API
        │
        ▼
UPI QR Code displayed on screen
        │
        ▼
check_status() polls Bonrix every ~3s
        │
        ▼
Payment confirmed → print_receipt()
```

API credentials (`PosName` and `ApiKey`) are stored in SPIFFS at `/pos_setting.txt` (Base64-encoded). Hardcoded fallback keys are used if the file is missing.

---

## 🖨️ Bluetooth Printer

The printer manager (`printer_manager.cpp`) handles:

- **Auto-scan** on boot for a saved MAC address
- **Auto-reconnect** if the printer goes out of range
- **Receipt printing** with order details, timestamp, and a verification QR code
- **Manual pairing** via the admin settings screen (BLE scan results are shown in a roller UI)

The target printer MAC is persisted in SPIFFS at `/printer_mac.txt`.

**BLE Service / Characteristic UUIDs (standard serial profile):**
```
Service:        49535343-fe7d-4ae5-8fa9-9fafd205e455
Characteristic: 49535343-8841-43f4-a8d4-ecbe34729bb3
```

---

## 🔐 Admin PIN System

A 4–6 digit numeric PIN protects access to the admin settings screen.

- PIN is stored in SPIFFS at `/pin.txt`
- Set/changed via the on-screen LVGL modal (`pin_manager.cpp`) or the web portal
- `show_pin_auth_dialog(callback)` — prompts for PIN, runs callback on success
- `show_pin_setup_dialog()` — sets a new PIN

---

## 🌐 Web Admin Portal

When the kiosk is on the network, visit `http://<kiosk-ip>/` in a browser to access:

| Route | Method | Action |
|---|---|---|
| `/` | GET | Dashboard — shows current PIN |
| `/set` | POST | Update the admin PIN |
| `/reboot` | POST | Remotely reboot the ESP32 |

Initialized via `init_web_server()`, polled in the main loop via `handle_web_server()`.

---

## 💰 Pricing Management

Default prices are defined in `pos_manager.cpp` and persisted in NVS:

| Category | Default Price |
|---|---|
| Adult Ticket | ₹150 |
| Child Ticket | ₹75 |
| Senior Ticket | ₹100 |
| Parking | ₹50 |
| Event (General) | ₹200 |
| Event (VIP) | ₹500 |

Prices can be updated from the admin settings screen. Changes are saved immediately to NVS via `save_pos_prices()`.

---

## 📶 WiFi Setup

- On first boot, the user connects via the on-screen WiFi settings UI
- Credentials are saved to NVS via `save_wifi_to_nvs()`
- On subsequent boots, `wifi_auto_reconnect_logic()` restores the connection automatically

---

## 📦 Dependencies

Install these libraries via the Arduino Library Manager or PlatformIO:

| Library | Purpose |
|---|---|
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | Display driver |
| [LVGL](https://lvgl.io/) | UI framework |
| [ArduinoJson](https://arduinojson.org/) | JSON parsing |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | BLE printer |
| [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32) | Base platform |
| WiFi, HTTPClient, WebServer, SPIFFS, Preferences | Built into ESP32 Arduino Core |

---

## 🚀 Getting Started

1. Clone this repository
2. Open `TicketWave.ino` in Arduino IDE (or PlatformIO)
3. Set your board to **ESP32S3 Dev Module**
4. Select the correct panel in `gfx_conf.h`
5. Upload your Bonrix API credentials to SPIFFS (`/pos_setting.txt`)
6. Flash and boot — the kiosk will start scanning for the paired printer automatically

---

## 📄 License

The QR code library (`qrcode.c` / `qrcode.h`) is licensed under the **MIT License** — © Richard Moore, © Project Nayuki.

All other project files are proprietary to the TicketWave project.

---

## 🙏 Acknowledgements

- [Richard Moore — QRCode Library](https://github.com/ricmoo/QRCode)
- [Project Nayuki — QR Code Generator](https://www.nayuki.io/page/qr-code-generator-library)
- [Bonrix Payment Gateway](http://api.kiosk.bonrix.in)
- [Elecrow CrowPanel](https://www.elecrow.com/crowpanel.html)
