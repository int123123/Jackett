### ESP32-S3 OV5640 Person Counter (ESP-IDF 5.4)

- Detects and counts persons in view using a YOLO model via ESP-DL.
- Works with `ESP32-S3-N16R8` and `OV5640` camera module.

#### Features
- OV5640 initialization with configurable pins (menuconfig).
- Runs YOLO (int8, 320x320) to detect COCO class 0 (person), counts persons.
- Logs results to UART periodically.

#### Prerequisites
- ESP-IDF 5.4 installed and exported (`. $IDF_PATH/export.sh`).
- `ESP32-S3-N16R8` board with PSRAM enabled.
- `OV5640` camera wired to the configured pins.

#### Model file
Place an int8 YOLO tiny model file at:
- `main/models/yolov3_tiny_320x320_int8.bin`

The build will embed this file. Without it, the app will run but exit early and warn in logs.

#### Build/Flash
```bash
cd esp_person_counter
idf.py set-target esp32s3
idf.py menuconfig   # Configure pins and thresholds under: ESP Person Counter
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

#### Menuconfig
- ESP Person Counter
  - Camera frame size (default QVGA)
  - YOLO input size (default 320)
  - Detection score threshold (default 40%)
  - NMS IoU threshold (default 50%)
  - Camera Pins (adjust to your wiring)

#### Notes
- Ensure PSRAM is enabled in `menuconfig` for S3 and `sdkconfig.defaults` enables it by default.
- You can lower frame size to improve speed; model input remains square.
- The person detection works even for partial body parts (head, arm) because YOLO bounding boxes will still classify as person.
