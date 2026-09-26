# SmartRoom – ESP32 IoT Automation System

## 3707ICT – Automation and Internet of Things

SmartRoom is an ESP32-based smart room automation system developed using Wokwi and PlatformIO.

The system monitors environmental conditions and automatically controls room devices to improve convenience, comfort, and energy efficiency.

## Hardware Components

- ESP32 microcontroller
- PIR motion sensor
- DHT22 temperature and humidity sensor
- LDR light sensor
- Door switch
- Servo motor for automated blinds
- Room lighting LED
- Status LED
- NeoPixel LED strip

## Main Features

### 1. Automatic Lighting
The room lighting automatically responds to detected occupancy and ambient light levels.

### 2. Automated Blinds
The servo motor adjusts the blinds based on the light sensor readings.

### 3. Temperature Monitoring
The system monitors room temperature and activates a simulated climate-control indicator when the temperature exceeds the configured threshold.

### 4. Door Monitoring
A switch simulates door opening and closing for room security monitoring.

### 5. Edge Intelligence
Rule-based occupancy detection combines motion readings and recent activity to determine whether the room is likely occupied.

### 6. Cloud Integration
The ESP32 uses Wi-Fi and MQTT to transmit sensor data to Adafruit IO for remote monitoring.

## Software

- Wokwi Simulator
- PlatformIO
- Arduino Framework
- Adafruit IO
- MQTT

## Repository Structure

- `src/main.cpp` – Main ESP32 firmware
- `diagram.json` – Wokwi circuit configuration
- `platformio.ini` – PlatformIO configuration
- `wokwi.toml` – Wokwi simulator configuration

## Project Team

3707ICT Group Project – Griffith University
Group members: Stella , Phoenix , Haruka
