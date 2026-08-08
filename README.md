# ESP32-CAM WiFi Robot

A WiFi-controlled robot built on an ESP32-CAM (AI-Thinker) with live video streaming and independent per-motor speed control over WebSockets, driven from a custom tactical HUD-style browser control panel.

## Demo

[YouTube link]


## Features

- Live MJPEG camera stream, served directly from the ESP32-CAM (no extra server needed)
- Real-time motor control over WebSocket (port 81), independent from the video HTTP server (port 80)
- Independent PWM speed control per motor (-255 to +255, sign = direction)
- Browser-based control panel with:
  - Live video feed with animated HUD-style corner brackets
  - Two vertical throttle sliders (Motor A / Motor B)
  - LINK mode to drive both motors together as one throttle
  - Emergency stop
  - Editable device IP (no code editing needed to connect)

## Hardware

| Component | Notes |
|---|---|
| ESP32-CAM (AI-Thinker) | Main controller + camera |
| DRV8833 motor driver | Dual H-bridge for 2 DC motors |
| 2x DC motors | Left/right drive |
| Separate motor power supply | See "Power" note below — do **not** share the ESP32-CAM's supply with the motors |

### Wiring

| DRV8833 pin | ESP32-CAM GPIO |
|---|---|
| IN1 | GPIO14 |
| IN2 | GPIO15 |
| IN3 | GPIO13 |
| IN4 | GPIO12 |

> **Note:** GPIO12 is a boot strapping pin on the ESP32. It defaults LOW at boot, which works fine here, but avoid any wiring that could pull it HIGH during power-up, or the board may fail to boot.

### Power

The ESP32-CAM's WiFi radio is sensitive to voltage sag. Running motors off the **same** supply as the ESP32-CAM can brown out the board the instant a motor kicks on, killing the video stream and WebSocket connection simultaneously.

Recommended:
- Separate battery for motors vs. ESP32-CAM logic, with only grounds tied together, **or**
- At minimum, a large bulk capacitor (1000µF+) across the motor supply near the DRV8833, and another (470–1000µF) at the ESP32-CAM's 5V input

## Firmware setup

1. Open `ESP32_CAM_Robot.ino` in Arduino IDE.
2. Set your WiFi credentials:
   ```cpp
   const char* ssid     = "YOUR_SSID";
   const char* password = "YOUR_PASSWORD";
   ```
3. Board settings: **AI Thinker ESP32-CAM**, partition scheme with enough space for the camera app (e.g. "Huge APP").
4. Flash with GPIO0 grounded (flashing mode), then reset with GPIO0 disconnected to run normally.
5. Open Serial Monitor at 115200 baud — it will print the assigned IP address once connected to WiFi.

## Control panel setup

1. Open `control_panel.html` in any browser (double-click it, or serve it locally — no build step required).
2. Type your ESP32's IP address (shown in Serial Monitor) into the **DEVICE IP** field (line 351) and tap **SET**.
3. Video feed and motor sliders will connect automatically.

Make sure the device you're using the control panel from is on the **same WiFi network** as the ESP32-CAM.

## WebSocket command protocol

Sent as plain text over the WebSocket connection on port 81:

| Command | Effect |
|---|---|
| `A:<int>` | Set Motor A speed, -255 (full reverse) to 255 (full forward) |
| `B:<int>` | Set Motor B speed, -255 to 255 |
| `STOP` | Stop both motors immediately |

## Challenges solved

- **WebSocket handshake never completing**: caused by a blocking `delay()` in the main loop preventing `webSocket.loop()` from running often enough to process the HTTP upgrade handshake in time.
- **Camera + controls freezing together under motor load**: a power brownout — the motors drawing current caused the ESP32's WiFi radio to drop momentarily. Fixed with a separate motor power supply and bulk capacitors.

