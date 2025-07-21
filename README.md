# ESP32 SteamVR Lighthouse Controller

A wireless controller for SteamVR V1 (HTC) and V2.0 lighthouse base stations using ESP32 microcontroller.

## Features

- **Web Interface**: Control lighthouses remotely through a web browser
- **Physical Buttons**: Hardware buttons for direct control
- **BLE Communication**: Uses Bluetooth Low Energy to communicate with base stations
- **WiFi Connectivity**: Connects to your local WiFi network
- **Status LED**: Visual feedback for operations
- **Dual Version Support**: Works with both V1 (HTC) and V2.0 lighthouses

## Hardware Requirements

- ESP32 development board (ESP32-S with CP2102 recommended)
- LED connected to pin 25 (optional)
- Push buttons connected to pins 32 and 33 (optional)
- USB cable for programming and power

## Software Requirements

- Visual Studio Code
- PlatformIO IDE extension
- NimBLE-Arduino library (automatically installed)
- JC_Button library (automatically installed)

## Configuration

### WiFi Setup
Edit the following lines in `src/main.cpp`:
```cpp
const char* ssid = "YOUR_WIFI_SSID";        // Replace with your WiFi name
const char* password = "YOUR_WIFI_PASSWORD"; // Replace with your WiFi password
```

### Lighthouse IDs (V1/HTC Base Stations)
Find the IDs on the back of your base stations and update:
```cpp
const char* lighthouseHTCIDs[] = {"YOUR_ID_1", "YOUR_ID_2"};
```

### V2.0 Base Stations
For V2.0 lighthouses, you can either:
- Set `lighthouseV2Filtering = false` to control all V2.0 base stations
- Or specify MAC addresses in the `lighthouseV2MACs[]` array

## Building and Flashing

### Using PlatformIO in VS Code:

1. **Open the project** in VS Code
2. **Connect your ESP32** via USB
3. **Build the project**: Click the checkmark (✓) in the PlatformIO toolbar
4. **Upload to ESP32**: Click the arrow (→) in the PlatformIO toolbar
5. **Open Serial Monitor**: Click the plug icon to monitor output

### Manual Build Commands:
```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Open serial monitor
pio device monitor
```

## Usage

### Web Interface
1. After flashing, open the Serial Monitor (115200 baud)
2. Note the IP address displayed when WiFi connects
3. Open that IP address in your web browser
4. Use the "Turn ON" and "Turn OFF" buttons

### Physical Controls
- Connect a button between pin 32 and ground for OFF control
- Connect a button between pin 33 and ground for ON control
- Connect an LED to pin 25 for status indication

### LED Status Codes
- **Solid during operation**: Command in progress
- **2 slow blinks**: Command successful
- **5 fast blinks**: Command failed (try again)
- **3 blinks at startup**: Setup complete

## Troubleshooting

### Compilation Errors
- Ensure PlatformIO IDE extension is installed
- Check that the ESP32 board package is properly installed
- Verify library dependencies are resolved

### Connection Issues
- Double-check WiFi credentials
- Ensure ESP32 is within range of your router
- Verify lighthouse IDs are correct (V1 base stations)

### BLE Issues
- Base stations must be in pairing/discoverable mode
- Ensure base stations are powered and not already connected to another device
- Try power cycling the base stations if they don't respond

## Technical Details

### Pin Configuration
- **Pin 25**: Status LED (optional)
- **Pin 32**: OFF button (pull-down)
- **Pin 33**: ON button (pull-down)

### BLE Services
- **V1 Service UUID**: `CB00`
- **V1 Characteristic UUID**: `CB01`
- **V2 Service UUID**: `00001523-1212-efde-1523-785feabcd124`
- **V2 Characteristic UUID**: `00001525-1212-efde-1523-785feabcd124`

### Commands
- **Turn ON**: `0x01`
- **Turn OFF**: `0x00`

## License

Based on the original work by nullstalgia, built upon NimBLE-Arduino examples.

## Contributing

Feel free to submit issues and enhancement requests!
