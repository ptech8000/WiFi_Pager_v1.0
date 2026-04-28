# 📟 WiFi Pager – ESP32-C3 Communication Tool

![Platform](https://img.shields.io/badge/Platform-ESP32--C3-blue)
![Framework](https://img.shields.io/badge/Framework-Arduino-00979D?logo=arduino)
![WiFi](https://img.shields.io/badge/WiFi-AP%20%2F%20Station-orange)
![License](https://img.shields.io/badge/License-MIT-green)

A battery‑friendly, WiFi‑connected pager built on the ESP32‑C3. It receives messages from any browser on the same network, displays them on a bright 1.3″ ST7789 screen, and alerts you with sound and visual notifications. **No cloud, no accounts, no latency** – just your own private, local‑network pager.

---

## ✨ Features

- **🌐 Web Interface** – Send messages, clear all, or test priority alerts from any device on the network.
- **🔴 Priority Messages** – Messages starting with `!` are displayed in red with a special alert.
- **📟 Hardware Buttons** – Six physical buttons for navigation, menu, delete, and power.
- **🔊 Buzzer Alerts** – Single/double beep, selectable from the menu.
- **💡 Power Management** – Automatic backlight dim, light sleep, and deep sleep after inactivity.
- **📊 Live Status API** – `/status` endpoint returns JSON with unread count and total messages.
- **📝 Preset Replies** – Send quick replies without typing.
- **🧠 Smart UI** – Cached rendering, frame‑rate limiting, and differential updates save CPU and power.

---

## 📦 Hardware Requirements

| Component | Details |
|-----------|---------|
| **MCU** | ESP32‑C3 Dev Module (or any ESP32‑C3 board) |
| **Display** | 1.3″ TFT ST7789 240×240 (SPI) |
| **Buzzer** | Active buzzer (5V, driven via GPIO) |
| **Buttons** | 6 × 12×12mm tactile switches |
| **Misc** | Breadboard, jumper wires, 5V power supply |

### 🧷 Pinout

```plaintext
ESP32-C3 Pin   →  Component
─────────────────────────────────
GPIO 2         →  Button UP
GPIO 3         →  Button DOWN
GPIO 4         →  Button SELECT
GPIO 5         →  Button BACK
GPIO 8         →  Button DELETE
GPIO 9         →  Button POWER
GPIO 10        →  TFT CS
GPIO 1         →  TFT RST
GPIO 0         →  TFT DC
GPIO 21        →  TFT Backlight
GPIO 7         →  TFT MOSI
GPIO 6         →  TFT SCLK
GPIO 20        →  Buzzer
