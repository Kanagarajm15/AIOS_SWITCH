# AIOS Switch

ESP32-based smart switch that receives ON/OFF commands from AIOS sensor controller via UDP with device ID validation, intelligent delayed OFF functionality, and physical button control.

## Hardware
- ESP32-C3
- Relay module connected to GPIO3
- Status LED connected to GPIO7
- Physical button connected to GPIO5
- Power supply

## Features
- **WiFi Configuration**: BLE-based WiFi credential setup
- **Device Authentication**: MAC-based device ID validation
- **UDP Command Processing**: Receives commands on port 9999
- **Intelligent Delayed OFF**: Variable delay based on sensor origin
  - TEMP origin: 60 seconds delay
  - MOTION origin: 5 seconds delay
  - Default: 6 seconds delay
- **Physical Button Control**: Manual ON/OFF toggle with debouncing
- **NVS Storage**: Persistent WiFi credentials storage
- **Firmware Version Display**: Shows version on startup
- **Modular Architecture**: Separated switch controller module

## Configuration
WiFi credentials are configured via BLE interface. No hardcoded credentials needed.

## GPIO Pins
- `RELAY_PIN`: GPIO3 (relay control)
- `LED_PIN`: GPIO7 (status LED)
- `SWITCH_PIN`: GPIO5 (physical button input with pull-up)

## Operation
- **Startup**: Device creates BLE advertisement with MAC-based name (SE-16A-SW-XX:XX:XX:XX:XX:XX)
- **WiFi Setup**: Connect via BLE to configure WiFi credentials
- **Command Processing**: Receives UDP JSON commands with device ID validation
- **Switch Control**: 
  - ON command: Immediate activation, cancels any pending OFF timer
  - OFF command: Delayed deactivation based on origin sensor type
  - Physical button: Manual toggle override
- **State Management**: Maintains current switch state and synchronizes physical outputs

## Command Format
```json
{
  "command": "ON/OFF",
  "source": "AIOS_SENSOR",
  "origin": "TEMP/MOTION/OTHER",
  "device_id": "XX:XX:XX:XX:XX:XX"
}
```

## Build & Flash
```bash
idf.py build
idf.py flash
idf.py monitor

# Or flash and monitor with specific COM port
idf.py -p COMxx flash monitor
```

