# AIOS Switch

Simple ESP32 switch that receives ON/OFF commands from AIOS sensor controller.

## Hardware
- ESP32 (any variant)
- Relay module connected to GPIO2
- Power supply

## Configuration
1. Update WiFi credentials in `switch_main.c`:
   - `WIFI_SSID`
   - `WIFI_PASS`

2. Update relay pin if needed:
   - `RELAY_PIN` (default: GPIO2)

## Build & Flash
```bash
idf.py build
idf.py flash monitor
```

## Operation
- Receives UDP commands on port 9999
- Processes JSON: `{"command": "ON/OFF", "source": "AIOS_SENSOR"}`
- Controls relay: ON = GPIO HIGH, OFF = GPIO LOW