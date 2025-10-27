# AIOS Switch

ESP32-based smart switch that receives ON/OFF commands from AIOS sensor controller via UDP with device ID validation and delayed OFF functionality.

## Hardware
- ESP32 (any variant)
- Relay module connected to GPIO3
- Power supply

## Features
- **WiFi Configuration**: BLE-based WiFi credential setup
- **Device Authentication**: MAC-based device ID validation
- **UDP Command Processing**: Receives commands on port 9999
- **Delayed OFF**: 10-second delay for OFF commands
- **NVS Storage**: Persistent WiFi credentials storage

## Configuration
WiFi credentials are configured via BLE interface. No hardcoded credentials needed.

## GPIO Pins
- `RELAY_PIN`: GPIO3 (relay control)
- `SWITCH_PIN`: GPIO5 (switch control)

## Build & Flash
```bash
idf.py build
idf.py flash monitor
```

## Operation
- **Startup**: Device creates BLE advertisement with MAC-based name
- **WiFi Setup**: Connect via BLE to configure WiFi credentials
- **Command Processing**: Receives UDP JSON commands with device ID validation
- **Switch Control**: 
  - ON command: Immediate activation
  - OFF command: 10-second delayed deactivation

## Command Format
```json
{
  "command": "ON/OFF",
  "source": "AIOS_SENSOR",
  "device_id": "XX:XX:XX:XX:XX:XX"
}
```

