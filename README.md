# ESP32 SteamVR Lighthouse Controller

A wireless controller for SteamVR V1 (HTC) and V2.0 lighthouse base stations using ESP32 microcontroller with web interface, MQTT integration, and Home Assistant support.

## Features

- **Enhanced Web Interface**: Control individual lighthouses with custom naming
- **MQTT Integration**: Full Home Assistant automation support
- **Physical Buttons**: Hardware buttons for direct control
- **BLE Communication**: Uses Bluetooth Low Energy with proper V1 command structure
- **WiFi Connectivity**: Connects to your local WiFi network
- **Individual Control**: Turn on/off specific lighthouses or all at once
- **Custom Naming**: Rename lighthouses through web interface
- **Status LED**: Visual feedback for operations
- **Master/Slave Support**: Control master base stations, slaves follow automatically
- **Home Assistant Ready**: Auto-discovery and automation integration

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
- PubSubClient library (automatically installed)

## Configuration

### WiFi Setup
Edit the following lines in `src/main.cpp`:
```cpp
const char* ssid = "YOUR_WIFI_SSID";        // Replace with your WiFi name
const char* password = "YOUR_WIFI_PASSWORD"; // Replace with your WiFi password
```

### Lighthouse Configuration (V1/HTC Base Stations)
Update the lighthouse mappings with your base station IDs:
```cpp
const LighthouseMapping lighthouseMappings[] = {
  {"C21347", "3BBF1347"},  // HTC BS C21347 -> 0x3BBF1347 from .ini (Master B)
  {"F862BD", "6BC162BD"}   // HTC BS F862BD -> 0x6BC162BD from .ini (Master B)
};
```

**Finding Your IDs:**
1. Check the back of your base stations for the advertised ID (like "C21347")
2. Use your original .ini file to find the full 8-character unique ID (like "3BBF1347")
3. Map the advertised ID to the full unique ID in the configuration

### MQTT Configuration (Optional)
Configure MQTT through the web interface at `http://[esp32-ip]/mqtt`:
- **MQTT Server**: Your Home Assistant IP address
- **Port**: 1883 (default)
- **Username/Password**: Your MQTT broker credentials
- **Base Topic**: `lighthouse` (or customize)

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
4. Control options available:
   - **All Lighthouses**: Turn all on/off at once
   - **Individual Control**: Turn specific lighthouses on/off
   - **Rename Function**: Click "Rename" to give lighthouses custom names
   - **MQTT Configuration**: Access via `/mqtt` endpoint

### Physical Controls
- Connect a button between pin 32 and ground for OFF control
- Connect a button between pin 33 and ground for ON control
- Connect an LED to pin 25 for status indication

### MQTT Control (Home Assistant)
The controller publishes/subscribes to these topics:
- `lighthouse/command` - Control all lighthouses (`on`/`off`)
- `lighthouse/lighthouse0/command` - Control first lighthouse
- `lighthouse/lighthouse1/command` - Control second lighthouse
- `lighthouse/lighthouse0/status` - First lighthouse status
- `lighthouse/lighthouse1/status` - Second lighthouse status
- `lighthouse/availability` - Controller online status

### Home Assistant Integration
Add to your `configuration.yaml`:
```yaml
mqtt:
  switch:
    - name: "VR Lighthouses All"
      command_topic: "lighthouse/command"
      state_topic: "lighthouse/status"
      payload_on: "on"
      payload_off: "off"
      icon: mdi:lighthouse

    - name: "VR Room 1 Master"
      command_topic: "lighthouse/lighthouse0/command"
      state_topic: "lighthouse/lighthouse0/status"
      payload_on: "on"
      payload_off: "off"
      icon: mdi:lighthouse

    - name: "VR Room 2 Master"
      command_topic: "lighthouse/lighthouse1/command"
      state_topic: "lighthouse/lighthouse1/status"
      payload_on: "on"
      payload_off: "off"
      icon: mdi:lighthouse
```

### LED Status Codes
- **Solid during operation**: Command in progress
- **2 slow blinks**: Command successful
- **5 fast blinks**: Command failed (try again)
- **3 blinks at startup**: Setup complete

## Master/Slave Operation

This controller is designed for dual-room VR setups where you have:
- **Room 1**: Master + Slave base station pair
- **Room 2**: Master + Slave base station pair

**How it works:**
- Configure only the **master** base station IDs
- When you turn ON a master, its slave automatically follows (~30 seconds)
- When you turn OFF a master, its slave shuts down when it can't find the master
- This matches the behavior of the original Python-based tools

## Troubleshooting

### Compilation Errors
- Ensure PlatformIO IDE extension is installed
- Check that the ESP32 board package is properly installed
- Verify library dependencies are resolved (NimBLE, JC_Button, PubSubClient)

### Connection Issues
- Double-check WiFi credentials in the code
- Ensure ESP32 is within range of your router
- Verify lighthouse IDs match your actual base stations

### BLE Issues
- Base stations must be powered and discoverable
- Check serial monitor for detailed BLE connection logs
- Verify the full 8-character unique IDs are correct
- Try power cycling base stations if they don't respond

### MQTT Issues
- Verify MQTT broker is running in Home Assistant
- Check MQTT credentials and server IP
- Monitor MQTT traffic in Home Assistant Developer Tools
- Ensure base topic matches between ESP32 and HA configuration

### Lighthouse Not Responding
- Verify you're using the correct 8-character unique ID, not just the advertised name
- Check that the lighthouse is the "B" (master) station, not "C" (slave)
- Ensure the lighthouse is not already controlled by SteamVR
- Try the working Python tool to verify the IDs are correct

## Technical Details

### Pin Configuration
- **Pin 25**: Status LED (optional)
- **Pin 32**: OFF button (pull-down)
- **Pin 33**: ON button (pull-down)

### V1 Command Structure (20 bytes)
- **Header**: `0x12`
- **Command**: `0x00` (wake) or `0x02` (sleep)
- **Timeout**: 2 bytes (big endian)
- **Unique ID**: 4 bytes (little endian)
- **Padding**: 12 bytes (zeros)

### BLE Services
- **V1 Service UUID**: `0000cb00-0000-1000-8000-00805f9b34fb`
- **V1 Write Characteristic**: `0000cb01-0000-1000-8000-00805f9b34fb`
- **V2 Service UUID**: `00001523-1212-efde-1523-785feabcd124`
- **V2 Characteristic UUID**: `00001525-1212-efde-1523-785feabcd124`

### Web Endpoints
- `/` - Main control interface
- `/on` - Turn all lighthouses on
- `/off` - Turn all lighthouses off
- `/on?id=0` - Turn specific lighthouse on
- `/off?id=0` - Turn specific lighthouse off
- `/rename?id=0&name=NewName` - Rename lighthouse
- `/mqtt` - MQTT configuration interface
- `/mqtt-save` - Save MQTT settings

## Voice Control Examples

With Home Assistant integration, you can use:
- **"Hey Google, turn on VR lighthouses"**
- **"Hey Google, turn off VR room one"**
- **"Alexa, turn on virtual reality tracking"**

## Automation Examples

**Auto VR Session Detection:**
```yaml
automation:
  - alias: "VR Ready Time"
    trigger:
      - platform: time
        at: "19:00:00"
    condition:
      - condition: state
        entity_id: person.your_name
        state: "home"
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.vr_lighthouses_all
```

## License

Based on the original work by nullstalgia, built upon NimBLE-Arduino examples.
Enhanced with MQTT integration and Home Assistant support.

## Contributing

Feel free to submit issues and enhancement requests!

## Credits

- Original ESP32 lighthouse control code by nullstalgia
- Command structure based on [BasestationPowerManager](https://github.com/cinfulsinamon/BasestationPowerManager)
- NimBLE Arduino library for BLE communication
- Home Assistant community for automation inspiration
