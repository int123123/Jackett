ESP-IDF (Windows, PowerShell) Quick Setup

1. Install ESP-IDF 5.4 via Online Installer. Include Git, Python, CMake, Ninja, toolchains.
2. Open "ESP-IDF 5.4 PowerShell" from Start menu.
3. Clone or copy this project folder.
4. In PowerShell:
   - idf.py set-target esp32
   - idf.py reconfigure
   - idf.py menuconfig  (set WiFi, MQTT, UART pins, PSRAM)
   - idf.py -p COMX flash monitor

Notes
- Board: AI-Thinker ESP32-CAM, OV2640, PSRAM enabled, XCLK 20MHz.
- If flashing fails, hold BOOT, tap RESET, release BOOT, retry.
- Topics published: home/radar/motion, home/camera/state, home/camera/person.
