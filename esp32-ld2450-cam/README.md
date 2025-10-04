# ESP32-CAM + LD2450 Radar: Presence-triggered Camera with MQTT

This ESP-IDF project runs on an ESP32-CAM module with PSRAM. It enables the LD2450 radar to trigger the camera; the camera stays on while a person is visible, then turns off after a grace period. Status and events are published over MQTT.

## Features
- LD2450 presence via UART frames (or GPIO input mode)
- ESP32-CAM init and light-weight person detection using skin ratio heuristic
- Power gating for radar via GPIO (optional)
- Wi‑Fi + MQTT with JSON reporting to `MQTT_BASE_TOPIC/<deviceId>/<state|event>`
- Configurable timings and thresholds via `menuconfig`

## Hardware
- Board: ESP32-CAM (AI-Thinker pinout by default)
- Radar: LD2450 connected to `UART2` (default TX=14, RX=15) or a GPIO presence pin
- Optional radar power pin `LD2450_POWER_GPIO` (set to `-1` to disable)

## Build and Flash
1. Open ESP-IDF PowerShell.
2. Set target and configure:
```powershell
idf.py set-target esp32
idf.py menuconfig
```
- Set `WiFi SSID/password`, `MQTT broker URI`, and camera/radar options as needed.

3. Build, flash, monitor (replace `COMx`):
```powershell
idf.py -p COM5 build flash monitor
```
Exit monitor with `Ctrl+]`.

## MQTT Topics
- State: `sensors/<deviceId>/state` with JSON: `{ "state": "radar|camera|boot", "detail": "..." }`
- Event: `sensors/<deviceId>/event` with JSON: `{ "event": "radar_motion|person_visible|person_back|camera_error", "msg": "..." }`

## Notes
- The person detector is a simple heuristic for low-power usage. For higher accuracy, integrate ESP-WHO (face/person detection) and set `PIXFORMAT_JPEG` + NN, but that increases CPU/RAM usage.
- Ensure PSRAM is enabled in `menuconfig` on ESP32-CAM modules.
