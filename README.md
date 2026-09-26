# SmartRoom — 3707ICT IoT Automation Project

Intelligent room automation system built on ESP32 for the 3707ICT
Smart IoT Automation System project. Monitors occupancy, temperature,
humidity and light, automates lighting and blinds, and publishes
telemetry to ThingSpeak over MQTT with TLS.

## Team
NA2: Thi Huyen My Bien, Phoenix Chio, Haruka Iwami

## Hardware
| Component | GPIO |
|---|---|
| PIR motion sensor | 14 |
| DHT22 temperature/humidity | 27 |
| LDR light sensor | 34 |
| Door slide switch | 26 |
| Servo (automated blinds) | 25 |
| Room LED / NeoPixel strip | 23 / 4 |
| Status / fault LED | 2 |
| Light override button | 32 |
| Blind override button | 33 |

## Features
- Rule-based automation: smart lighting, automatic blinds, temperature
  alert with hysteresis, door/security monitoring
- Named occupancy states (Active / Idle / Unoccupied) with a timeout
- Manual override buttons for light and blinds, auto-reset when the
  room empties
- Failsafe on DHT22 sensor fault (safe state + fast LED flash)
- MQTT publishing to ThingSpeak over TLS (port 8883), with MQTT
  username/password authentication

## Cloud dashboard
ThingSpeak channel fields:
1. Temperature, 2. Humidity, 3. Light, 4. Occupancy Score,
5. Room Light, 6. Blinds Angle, 7. Door, 8. Alert

## Setup
1. Install [PlatformIO](https://platformio.org/) and the
   [Wokwi extension](https://docs.wokwi.com/vscode/getting-started)
   in VS Code.
2. Copy `include/secrets.example.h` to `include/secrets.h` and fill
   in your own ThingSpeak channel ID and MQTT device credentials.
3. Build with PlatformIO (`Ctrl/Cmd+Alt+B` or the checkmark icon).
4. Simulate with `Wokwi: Start Simulator` from the command palette.

## Project structure
- `src/main.cpp` — main firmware
- `include/secrets.h` — credentials (gitignored, not committed)
- `include/secrets.example.h` — credentials template
- `diagram.json`, `wokwi.toml` — Wokwi simulation config