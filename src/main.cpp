/** SteamVR V1 HTC / SteamVR V2.0 Lighthouse/Base Station ESP32 Control:
 *
 *  Created: on March 25 2021
 *      Author: nullstalgia
 *
 * Based on NimBLE BLE Client Example:
 * https://github.com/h2zero/NimBLE-Arduino/blob/master/examples/NimBLE_Client/NimBLE_Client.ino
 *
 */

#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 5
#define NIMBLE_MAX_CONNECTIONS CONFIG_BT_NIMBLE_MAX_CONNECTIONS

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "JC_Button.h"
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <Preferences.h>

// For Version 1 (HTC) Base Stations:

// You can place up to CONFIG_BT_NIMBLE_MAX_CONNECTIONS amount of IDs here.
// (ESP32 max is 9) Find this on the back of your Base Station. Technically you
// only need to enter the B station ID, but C will look around for B for a while
// before shutting down, so I personally put both in :) This is required to turn
// the Base Station off immediately, and as such, this app is configured so if
// you want this to even turn it ON, you need the ID in here.
// My Base Stations for example: "7F35E5C5", "034996AB"  
// Based on your .ini file: 0x3BBF1347, 0x6BC162BD, etc.
// Mapping: Advertised Name -> Full 8-character ID 
struct LighthouseMapping {
  const char* advertisedId;    // Last part of "HTC BS XXXXXX"
  const char* fullId;          // Full 8-character ID for commands
};

const LighthouseMapping lighthouseMappings[] = {
  {"C21347", "3BBF1347"},  // HTC BS C21347 -> 0x3BBF1347 from .ini (Master B)
  {"F862BD", "6BC162BD"}   // HTC BS F862BD -> 0x6BC162BD from .ini (Master B)
};

// Custom names for each lighthouse (can be changed via web interface)
String lighthouseNames[] = {"Room 1 Master (C21347)", "Room 2 Master (F862BD)"};

// Helper function to get full ID from advertised ID
const char* getFullIdFromAdvertised(const char* advertisedId) {
  for (int i = 0; i < sizeof(lighthouseMappings) / sizeof(lighthouseMappings[0]); i++) {
    if (strcmp(advertisedId, lighthouseMappings[i].advertisedId) == 0) {
      return lighthouseMappings[i].fullId;
    }
  }
  return nullptr;
}

// Helper function to get mapping index from advertised ID
int getMappingIndexFromAdvertised(const char* advertisedId) {
  for (int i = 0; i < sizeof(lighthouseMappings) / sizeof(lighthouseMappings[0]); i++) {
    if (strcmp(advertisedId, lighthouseMappings[i].advertisedId) == 0) {
      return i;
    }
  }
  return -1;
}

// For Version 2.0 Base Stations:

// You can have the app just turn on/off every 2.0 Base Station it finds by
// turning this false If it's true, it will use the following filter to turn
// on/off Base Stations (in case you have multiple sets for some reason)
// Also no, this app is not set up for changing RF channels
const bool lighthouseV2Filtering = false;
// Enter the full MAC Address of your desired Base Stations below
// You can find this with NRF Connect or a similar app on your smartphone
// Example: lighthouseLHBMACs[] = {NimBLEAddress("D3:EA:E4:A4:58:DF")};

static NimBLEAddress lighthouseV2MACs[] = {};

// Connect an LED to this pin to get info on if there was an issue during the
// command (if an error does happen, just try it again a couple times) Just a
// couple slow-ish blinks: Success Many fast blinks: Error, try again
const uint8_t ledPin = 25;

Button offButton(32);  // define the pin for button (pull to ground to activate)

Button onButton(33);  // define the pin for button (pull to ground to activate)

const char* ssid = "Oben";        // TODO: set your WiFi SSID
const char* password = "06385538"; // TODO: set your WiFi password

WebServer server(80);

// MQTT Configuration
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Preferences preferences;

// MQTT Settings (will be loaded from preferences)
String mqttServer = "";
int mqttPort = 1883;
String mqttUsername = "";
String mqttPassword = "";
String mqttTopic = "lighthouse";
bool mqttEnabled = false;
unsigned long lastMqttReconnectAttempt = 0;

// The remote service we wish to connect to.
static NimBLEUUID serviceUUIDHTC("0000cb00-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in.
static NimBLEUUID characteristicUUIDHTC("0000cb01-0000-1000-8000-00805f9b34fb");

static NimBLEUUID serviceUUIDV2("00001523-1212-efde-1523-785feabcd124");
static NimBLEUUID characteristicUUIDV2("00001525-1212-efde-1523-785feabcd124");

enum { NOTHING = 0, TURN_ON_PERM = 1, TURN_OFF = 2 };

#define MAX_DISCOVERABLE_LH CONFIG_BT_NIMBLE_MAX_CONNECTIONS

static NimBLEAdvertisedDevice* discoveredLighthouses[MAX_DISCOVERABLE_LH];

uint8_t discoveredLighthouseVersions[MAX_DISCOVERABLE_LH];

// Store the lighthouse ID index for each discovered lighthouse
int discoveredLighthouseIds[MAX_DISCOVERABLE_LH];

uint8_t currentCommand = NOTHING;

int lighthouseCount = 0;

int targetLighthouseIndex = -1; // -1 means all lighthouses, 0+ means specific lighthouse

void scanEndedCB(NimBLEScanResults results);

// MQTT function declarations
bool connectMqtt();
void loadMqttConfig();
void saveMqttConfig();
void publishMqttStatus();
void handleMqttConfig();
void handleMqttSave();

// Function to create lighthouse command with proper structure
void makeLighthouseCommand(uint8_t* buffer, uint8_t cmdId, uint16_t timeout, const char* uniqueId) {
  // Clear buffer
  memset(buffer, 0, 20);
  
  // Header
  buffer[0] = 0x12;
  
  // Command ID (0x00 = wake, 0x02 = sleep)
  buffer[1] = cmdId;
  
  // Timeout (2 bytes, big endian)
  buffer[2] = (timeout >> 8) & 0xFF;
  buffer[3] = timeout & 0xFF;
  
  // Convert unique ID from hex string to 4 bytes (little endian)
  uint32_t id = strtoul(uniqueId, NULL, 16);
  buffer[4] = id & 0xFF;
  buffer[5] = (id >> 8) & 0xFF;
  buffer[6] = (id >> 16) & 0xFF;
  buffer[7] = (id >> 24) & 0xFF;
  
  // Remaining 12 bytes are already zero from memset
}

static bool readyToConnect = false;
static uint32_t scanTime = 5; /** 0 = scan forever. In seconds */

void startScanAndSetCommand(uint8_t command) {
  digitalWrite(ledPin, HIGH);
  lighthouseCount = 0;
  for (uint8_t i = 0; i < MAX_DISCOVERABLE_LH; i++) {
    discoveredLighthouses[i] = nullptr;
    discoveredLighthouseVersions[i] = 0;
    discoveredLighthouseIds[i] = -1;
  }
  NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
  currentCommand = command;
}

void handleRoot() {
  String html = "<html><head><title>Lighthouse Controller</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }";
  html += ".container { max-width: 800px; margin: 0 auto; }";
  html += ".lighthouse { border: 1px solid #ddd; margin: 10px 0; padding: 15px; border-radius: 8px; background-color: white; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += ".lighthouse h3 { margin-top: 0; color: #333; border-bottom: 1px solid #eee; padding-bottom: 10px; }";
  html += ".button { font-size: 14px; padding: 8px 15px; margin: 5px; border: none; border-radius: 4px; cursor: pointer; text-decoration: none; display: inline-block; }";
  html += ".on-btn { background-color: #4CAF50; color: white; }";
  html += ".off-btn { background-color: #f44336; color: white; }";
  html += ".edit-btn { background-color: #2196F3; color: white; }";
  html += ".save-btn { background-color: #FF9800; color: white; }";
  html += ".on-btn:hover { background-color: #45a049; }";
  html += ".off-btn:hover { background-color: #da190b; }";
  html += ".edit-btn:hover { background-color: #1976D2; }";
  html += ".save-btn:hover { background-color: #F57C00; }";
  html += ".status { font-weight: bold; margin: 15px 0; padding: 10px; background-color: #e3f2fd; border-radius: 4px; }";
  html += ".lighthouse-id { font-size: 12px; color: #666; margin: 5px 0; }";
  html += ".name-input { padding: 5px; margin: 5px; border: 1px solid #ccc; border-radius: 3px; }";
  html += ".controls { margin-top: 10px; }";
  html += "h1 { text-align: center; color: #333; }";
  html += ".discovery-info { text-align: center; margin: 20px 0; font-weight: bold; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>SteamVR Lighthouse Controller</h1>";
  
  // Status section
  html += "<div class='status'>Status: ";
  if (currentCommand == NOTHING) {
    html += "Ready";
  } else if (currentCommand == TURN_ON_PERM) {
    html += "Turning ON...";
  } else if (currentCommand == TURN_OFF) {
    html += "Turning OFF...";
  }
  html += "</div>";
  
  // All lighthouses control
  html += "<div class='lighthouse'>";
  html += "<h3>All Lighthouses</h3>";
  html += "<div class='controls'>";
  html += "<a href='/on'><button class='button on-btn'>Turn All ON</button></a>";
  html += "<a href='/off'><button class='button off-btn'>Turn All OFF</button></a>";
  html += "</div>";
  html += "</div>";
  
  // Individual lighthouse controls
  for (int i = 0; i < sizeof(lighthouseMappings) / sizeof(lighthouseMappings[0]); i++) {
    html += "<div class='lighthouse'>";
    html += "<h3>" + lighthouseNames[i] + "</h3>";
    html += "<div class='lighthouse-id'>ID: " + String(lighthouseMappings[i].advertisedId) + "</div>";
    
    html += "<div class='controls'>";
    html += "<a href='/on?id=" + String(i) + "'><button class='button on-btn'>Turn ON</button></a>";
    html += "<a href='/off?id=" + String(i) + "'><button class='button off-btn'>Turn OFF</button></a>";
    html += "<button class='button edit-btn' onclick='editName(" + String(i) + ")'>Rename</button>";
    html += "</div>";
    
    // Hidden rename form
    html += "<div id='rename-" + String(i) + "' style='display:none; margin-top:10px;'>";
    html += "<input type='text' id='name-" + String(i) + "' class='name-input' value='" + lighthouseNames[i] + "' placeholder='Enter new name'>";
    html += "<button class='button save-btn' onclick='saveName(" + String(i) + ")'>Save</button>";
    html += "<button class='button off-btn' onclick='cancelEdit(" + String(i) + ")'>Cancel</button>";
    html += "</div>";
    
    html += "</div>";
  }
  
  html += "<div class='discovery-info'>Discovered Lighthouses: " + String(lighthouseCount) + "</div>";
  
  // MQTT status and configuration link
  html += "<div style='text-align: center; margin: 20px 0;'>";
  html += "<strong>MQTT Status:</strong> ";
  if (mqttEnabled) {
    html += "<span style='color: " + String(mqttClient.connected() ? "green" : "red") + "'>";
    html += mqttClient.connected() ? "Connected" : "Disconnected";
    html += "</span>";
  } else {
    html += "<span style='color: gray'>Disabled</span>";
  }
  html += " | <a href='/mqtt' class='button edit-btn'>Configure MQTT</a>";
  html += "</div>";
  
  // JavaScript for rename functionality
  html += "<script>";
  html += "function editName(id) {";
  html += "  document.getElementById('rename-' + id).style.display = 'block';";
  html += "}";
  html += "function cancelEdit(id) {";
  html += "  document.getElementById('rename-' + id).style.display = 'none';";
  html += "}";
  html += "function saveName(id) {";
  html += "  var newName = document.getElementById('name-' + id).value;";
  html += "  if (newName.trim() !== '') {";
  html += "    window.location.href = '/rename?id=' + id + '&name=' + encodeURIComponent(newName);";
  html += "  }";
  html += "}";
  html += "</script>";
  
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleOn() {
  if (currentCommand == NOTHING) {
    String targetId = server.arg("id");
    if (targetId.length() > 0) {
      // Individual lighthouse control
      int lighthouseIndex = targetId.toInt();
      if (lighthouseIndex >= 0 && lighthouseIndex < sizeof(lighthouseMappings) / sizeof(lighthouseMappings[0])) {
        targetLighthouseIndex = lighthouseIndex;
        startScanAndSetCommand(TURN_ON_PERM);
        server.send(200, "text/html", "<html><body><h1>Turning ON Lighthouse " + String(lighthouseMappings[lighthouseIndex].advertisedId) + "...</h1><p><a href='/'>Back</a></p></body></html>");
      } else {
        server.send(400, "text/html", "<html><body><h1>Invalid lighthouse index</h1><p><a href='/'>Back</a></p></body></html>");
      }
    } else {
      // All lighthouses control
      targetLighthouseIndex = -1; // -1 means all lighthouses
      startScanAndSetCommand(TURN_ON_PERM);
      server.send(200, "text/html", "<html><body><h1>Turning ON All Lighthouses...</h1><p><a href='/'>Back</a></p></body></html>");
    }
  } else {
    server.send(200, "text/html", "<html><body><h1>Command already in progress</h1><p><a href='/'>Back</a></p></body></html>");
  }
}

void handleOff() {
  if (currentCommand == NOTHING) {
    String targetId = server.arg("id");
    if (targetId.length() > 0) {
      // Individual lighthouse control
      int lighthouseIndex = targetId.toInt();
      if (lighthouseIndex >= 0 && lighthouseIndex < sizeof(lighthouseMappings) / sizeof(lighthouseMappings[0])) {
        targetLighthouseIndex = lighthouseIndex;
        startScanAndSetCommand(TURN_OFF);
        server.send(200, "text/html", "<html><body><h1>Turning OFF Lighthouse " + String(lighthouseMappings[lighthouseIndex].advertisedId) + "...</h1><p><a href='/'>Back</a></p></body></html>");
      } else {
        server.send(400, "text/html", "<html><body><h1>Invalid lighthouse index</h1><p><a href='/'>Back</a></p></body></html>");
      }
    } else {
      // All lighthouses control
      targetLighthouseIndex = -1; // -1 means all lighthouses
      startScanAndSetCommand(TURN_OFF);
      server.send(200, "text/html", "<html><body><h1>Turning OFF All Lighthouses...</h1><p><a href='/'>Back</a></p></body></html>");
    }
  } else {
    server.send(200, "text/html", "<html><body><h1>Command already in progress</h1><p><a href='/'>Back</a></p></body></html>");
  }
}

void handleRename() {
  String targetId = server.arg("id");
  String newName = server.arg("name");
  
  if (targetId.length() > 0 && newName.length() > 0) {
    int lighthouseIndex = targetId.toInt();
    if (lighthouseIndex >= 0 && lighthouseIndex < sizeof(lighthouseMappings) / sizeof(lighthouseMappings[0])) {
      // Limit name length and sanitize
      newName = newName.substring(0, 20); // Max 20 characters
      lighthouseNames[lighthouseIndex] = newName;
      
      // Redirect back to main page
      server.sendHeader("Location", "/");
      server.send(302, "text/plain", "");
    } else {
      server.send(400, "text/html", "<html><body><h1>Invalid lighthouse index</h1><p><a href='/'>Back</a></p></body></html>");
    }
  } else {
    server.send(400, "text/html", "<html><body><h1>Missing parameters</h1><p><a href='/'>Back</a></p></body></html>");
  }
}

void handleMqttConfig() {
  String html = "<html><head><title>MQTT Configuration</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }";
  html += ".container { max-width: 600px; margin: 0 auto; background-color: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += ".form-group { margin-bottom: 15px; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += "input[type='text'], input[type='password'], input[type='number'] { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }";
  html += "input[type='checkbox'] { margin-right: 8px; }";
  html += ".button { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-right: 10px; }";
  html += ".button:hover { background-color: #45a049; }";
  html += ".back-btn { background-color: #6c757d; }";
  html += ".back-btn:hover { background-color: #5a6268; }";
  html += "h1 { color: #333; }";
  html += ".status { margin: 10px 0; padding: 10px; background-color: #e3f2fd; border-radius: 4px; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>MQTT Configuration</h1>";
  
  // Show current MQTT status
  html += "<div class='status'>";
  html += "<strong>Current Status:</strong> ";
  if (mqttEnabled) {
    html += mqttClient.connected() ? "Connected" : "Enabled but not connected";
  } else {
    html += "Disabled";
  }
  html += "</div>";
  
  html += "<form method='POST' action='/mqtt-save'>";
  
  html += "<div class='form-group'>";
  html += "<label><input type='checkbox' name='enabled' " + String(mqttEnabled ? "checked" : "") + "> Enable MQTT</label>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='server'>MQTT Server:</label>";
  html += "<input type='text' id='server' name='server' value='" + mqttServer + "' placeholder='192.168.1.100'>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='port'>Port:</label>";
  html += "<input type='number' id='port' name='port' value='" + String(mqttPort) + "' placeholder='1883'>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='username'>Username:</label>";
  html += "<input type='text' id='username' name='username' value='" + mqttUsername + "' placeholder='Optional'>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='password'>Password:</label>";
  html += "<input type='password' id='password' name='password' value='" + mqttPassword + "' placeholder='Optional'>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='topic'>Base Topic:</label>";
  html += "<input type='text' id='topic' name='topic' value='" + mqttTopic + "' placeholder='lighthouse'>";
  html += "</div>";
  
  html += "<div style='margin-top: 20px;'>";
  html += "<p><strong>MQTT Topics that will be used:</strong></p>";
  html += "<ul>";
  html += "<li><code>" + mqttTopic + "/command</code> - Send 'on' or 'off' to control all lighthouses</li>";
  html += "<li><code>" + mqttTopic + "/lighthouse0/command</code> - Control individual lighthouse</li>";
  html += "<li><code>" + mqttTopic + "/lighthouse1/command</code> - Control individual lighthouse</li>";
  html += "<li><code>" + mqttTopic + "/lighthouse0/status</code> - Lighthouse status (published)</li>";
  html += "<li><code>" + mqttTopic + "/lighthouse0/name</code> - Lighthouse name (published)</li>";
  html += "</ul>";
  html += "</div>";
  
  html += "<button type='submit' class='button'>Save Configuration</button>";
  html += "<a href='/' class='button back-btn'>Back to Main</a>";
  html += "</form>";
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleMqttSave() {
  mqttEnabled = server.hasArg("enabled");
  mqttServer = server.arg("server");
  mqttPort = server.arg("port").toInt();
  if (mqttPort <= 0) mqttPort = 1883;
  mqttUsername = server.arg("username");
  mqttPassword = server.arg("password");
  mqttTopic = server.arg("topic");
  if (mqttTopic.length() == 0) mqttTopic = "lighthouse";
  
  // Save configuration
  saveMqttConfig();
  
  // Disconnect existing MQTT connection if any
  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
  
  // Try to connect with new settings
  if (mqttEnabled) {
    connectMqtt();
  }
  
  // Redirect back to MQTT config page
  server.sendHeader("Location", "/mqtt");
  server.send(302, "text/plain", "");
}

class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) {
    Serial.println("Connected");
  }

  void onDisconnect(NimBLEClient* pClient) {
    Serial.print(pClient->getPeerAddress().toString().c_str());
    Serial.println(" Disconnected - Starting scan");
  }

  bool onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) {
    if (params->itvl_min < 24) {
      return false;
    } else if (params->itvl_max > 40) {
      return false;
    } else if (params->latency > 2) {
      return false;
    } else if (params->supervision_timeout > 100) {
      return false;
    }
    return true;
  }
};

static ClientCallbacks clientCB;

class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    Serial.print("Advertised Device found: ");
    Serial.println(advertisedDevice->toString().c_str());

    if (lighthouseCount >= MAX_DISCOVERABLE_LH) {
      return;
    }

    // Check for V1 (HTC) Base Stations
    if (advertisedDevice->isAdvertisingService(serviceUUIDHTC)) {
      String advertisedName = advertisedDevice->getName().c_str();
      Serial.println("=== HTC Base Station Found ===");
      Serial.printf("Advertised Name: '%s'\n", advertisedName.c_str());
      Serial.printf("MAC Address: %s\n", advertisedDevice->getAddress().toString().c_str());
      
      for (int i = 0; i < sizeof(lighthouseMappings) / sizeof(lighthouseMappings[0]); i++) {
        Serial.printf("Checking against mapping[%d]: advertised='%s', fullId='%s'\n", i, lighthouseMappings[i].advertisedId, lighthouseMappings[i].fullId);
        if (advertisedName.indexOf(lighthouseMappings[i].advertisedId) >= 0) {
          // Check if we're targeting a specific lighthouse
          if (targetLighthouseIndex >= 0 && targetLighthouseIndex != i) {
            Serial.printf("Skipping - not target lighthouse (want %d, this is %d)\n", targetLighthouseIndex, i);
            continue; // Skip this lighthouse if we're not targeting it
          }
          
          Serial.printf("✅ MATCH! Found V1 Lighthouse: %s (Full ID: %s)\n", advertisedName.c_str(), lighthouseMappings[i].fullId);
          discoveredLighthouses[lighthouseCount] = advertisedDevice;
          discoveredLighthouseVersions[lighthouseCount] = 1;
          discoveredLighthouseIds[lighthouseCount] = i; // Store the mapping index
          lighthouseCount++;
          break;
        }
      }
      Serial.println("===============================");
    }
    // Check for V2 Base Stations
    else if (advertisedDevice->isAdvertisingService(serviceUUIDV2)) {
      bool shouldAdd = true;
      
      if (lighthouseV2Filtering) {
        shouldAdd = false;
        for (int i = 0; i < sizeof(lighthouseV2MACs) / sizeof(lighthouseV2MACs[0]); i++) {
          if (advertisedDevice->getAddress().equals(lighthouseV2MACs[i])) {
            shouldAdd = true;
            break;
          }
        }
      }
      
      if (shouldAdd) {
        Serial.printf("Found V2 Lighthouse: %s\n", advertisedDevice->getAddress().toString().c_str());
        discoveredLighthouses[lighthouseCount] = advertisedDevice;
        discoveredLighthouseVersions[lighthouseCount] = 2;
        discoveredLighthouseIds[lighthouseCount] = -1; // V2 doesn't need ID index
        lighthouseCount++;
      }
    }
  }
};

bool sendLighthouseCommands() {
  // Clean up existing clients
  auto clientList = NimBLEDevice::getClientList();
  for (auto client : *clientList) {
    NimBLEDevice::deleteClient(client);
  }

  if (lighthouseCount == 0) {
    Serial.println("No lighthouses found!");
    return false;
  }

  bool success = true;

  for (int i = 0; i < lighthouseCount; i++) {
    if (discoveredLighthouses[i] == nullptr) continue;

    if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
      Serial.println("Max clients reached");
      break;
    }

    NimBLEClient* pClient = nullptr;

    if (NimBLEDevice::getClientListSize()) {
      pClient = NimBLEDevice::getClientByPeerAddress(discoveredLighthouses[i]->getAddress());
      if (pClient) {
        if (!pClient->connect(discoveredLighthouses[i], false)) {
          Serial.println("Reconnect failed");
          continue;
        }
        Serial.println("Reconnected client");
      }
    }

    if (!pClient) {
      if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
        Serial.println("Max clients reached - Unable to create client");
        continue;
      }

      pClient = NimBLEDevice::createClient();
      Serial.println("New client created");

      pClient->setClientCallbacks(&clientCB, false);
      pClient->setConnectionParams(12, 12, 0, 51);
      pClient->setConnectTimeout(5);

      // Try to connect with retry
      bool connected = false;
      for (int retry = 0; retry < 3; retry++) {
        Serial.printf("Connection attempt %d/3 for %s\n", retry + 1, discoveredLighthouses[i]->getAddress().toString().c_str());
        if (pClient->connect(discoveredLighthouses[i])) {
          connected = true;
          break;
        }
        if (retry < 2) {
          Serial.println("Connection failed, retrying...");
          delay(1000); // Wait 1 second before retry
        }
      }
      
      if (!connected) {
        NimBLEDevice::deleteClient(pClient);
        Serial.printf("Failed to connect after 3 attempts to %s\n", discoveredLighthouses[i]->getAddress().toString().c_str());
        success = false;
        continue;
      }
    }

    Serial.print("Connected to: ");
    Serial.println(pClient->getPeerAddress().toString().c_str());

    NimBLERemoteService* pSvc = nullptr;
    NimBLERemoteCharacteristic* pChr = nullptr;

    // Handle V1 (HTC) Base Stations
    if (discoveredLighthouseVersions[i] == 1) {
      Serial.printf("Processing V1 lighthouse %d/%d\n", i + 1, lighthouseCount);
      pSvc = pClient->getService(serviceUUIDHTC);
      if (pSvc) {
        pChr = pSvc->getCharacteristic(characteristicUUIDHTC);
        if (pChr) {
          if (pChr->canWrite()) {
            // Create proper 20-byte command structure
            uint8_t command[20];
            int mappingIndex = discoveredLighthouseIds[i];
            const char* lighthouseId = lighthouseMappings[mappingIndex].fullId;
            
            if (currentCommand == TURN_ON_PERM) {
              // Wake command: 0x00 with no timeout
              makeLighthouseCommand(command, 0x00, 0, lighthouseId);
              Serial.printf("Sending WAKE command (0x00) with ID %s\n", lighthouseId);
            } else {
              // Sleep command: 0x02 with timeout 1
              makeLighthouseCommand(command, 0x02, 1, lighthouseId);
              Serial.printf("Sending SLEEP command (0x02) with ID %s\n", lighthouseId);
            }
            
            // Debug: Print command bytes
            Serial.print("Command bytes: ");
            for (int j = 0; j < 20; j++) {
              Serial.printf("%02X ", command[j]);
            }
            Serial.println();
            
            if (pChr->writeValue(command, 20)) {
              Serial.printf("✅ Sent V1 command to %s\n", pClient->getPeerAddress().toString().c_str());
            } else {
              Serial.printf("❌ Failed to send V1 command to %s\n", pClient->getPeerAddress().toString().c_str());
              success = false;
            }
          } else {
            Serial.printf("❌ Characteristic not writable for %s\n", pClient->getPeerAddress().toString().c_str());
            success = false;
          }
        } else {
          Serial.printf("❌ Characteristic not found for %s\n", pClient->getPeerAddress().toString().c_str());
          success = false;
        }
      } else {
        Serial.printf("❌ Service not found for %s\n", pClient->getPeerAddress().toString().c_str());
        success = false;
      }
    }
    // Handle V2 Base Stations
    else if (discoveredLighthouseVersions[i] == 2) {
      Serial.printf("Processing V2 lighthouse %d/%d\n", i + 1, lighthouseCount);
      pSvc = pClient->getService(serviceUUIDV2);
      if (pSvc) {
        pChr = pSvc->getCharacteristic(characteristicUUIDV2);
        if (pChr) {
          if (pChr->canWrite()) {
            uint8_t command = (currentCommand == TURN_ON_PERM) ? 0x01 : 0x00;
            if (pChr->writeValue(&command, 1)) {
              Serial.printf("✅ Sent V2 command %02X to %s\n", command, pClient->getPeerAddress().toString().c_str());
            } else {
              Serial.printf("❌ Failed to send V2 command to %s\n", pClient->getPeerAddress().toString().c_str());
              success = false;
            }
          } else {
            Serial.printf("❌ V2 Characteristic not writable for %s\n", pClient->getPeerAddress().toString().c_str());
            success = false;
          }
        } else {
          Serial.printf("❌ V2 Characteristic not found for %s\n", pClient->getPeerAddress().toString().c_str());
          success = false;
        }
      } else {
        Serial.printf("❌ V2 Service not found for %s\n", pClient->getPeerAddress().toString().c_str());
        success = false;
      }
    }

    delay(100); // Small delay between commands
  }

  return success;
}

void scanEndedCB(NimBLEScanResults results) {
  Serial.print("Scan Ended. Found ");
  Serial.print(lighthouseCount);
  Serial.println(" lighthouses");

  readyToConnect = true;
}

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(ledPin, HIGH);
    delay(delayMs);
    digitalWrite(ledPin, LOW);
    delay(delayMs);
  }
}

// MQTT Configuration Functions
void loadMqttConfig() {
  preferences.begin("lighthouse", false);
  mqttServer = preferences.getString("mqtt_server", "");
  mqttPort = preferences.getInt("mqtt_port", 1883);
  mqttUsername = preferences.getString("mqtt_user", "");
  mqttPassword = preferences.getString("mqtt_pass", "");
  mqttTopic = preferences.getString("mqtt_topic", "lighthouse");
  mqttEnabled = preferences.getBool("mqtt_enabled", false);
  preferences.end();
}

void saveMqttConfig() {
  preferences.begin("lighthouse", false);
  preferences.putString("mqtt_server", mqttServer);
  preferences.putInt("mqtt_port", mqttPort);
  preferences.putString("mqtt_user", mqttUsername);
  preferences.putString("mqtt_pass", mqttPassword);
  preferences.putString("mqtt_topic", mqttTopic);
  preferences.putBool("mqtt_enabled", mqttEnabled);
  preferences.end();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.printf("MQTT message received: %s = %s\n", topic, message.c_str());
  
  String topicStr = String(topic);
  
  // Handle lighthouse commands
  if (topicStr.endsWith("/command")) {
    if (message == "on") {
      startScanAndSetCommand(TURN_ON_PERM);
    } else if (message == "off") {
      startScanAndSetCommand(TURN_OFF);
    }
  }
  // Handle individual lighthouse commands
  else if (topicStr.indexOf("/lighthouse") > 0) {
    // Extract lighthouse index from topic like "lighthouse/lighthouse0/command"
    int startIdx = topicStr.indexOf("/lighthouse") + 11;
    int endIdx = topicStr.indexOf("/", startIdx);
    if (endIdx > startIdx) {
      String lighthouseIndexStr = topicStr.substring(startIdx, endIdx);
      int lighthouseIndex = lighthouseIndexStr.toInt();
      
      if (lighthouseIndex >= 0 && lighthouseIndex < 2) { // Only support 2 masters
        targetLighthouseIndex = lighthouseIndex;
        if (message == "on") {
          startScanAndSetCommand(TURN_ON_PERM);
        } else if (message == "off") {
          startScanAndSetCommand(TURN_OFF);
        }
      }
    }
  }
}

bool connectMqtt() {
  if (!mqttEnabled || mqttServer.length() == 0) {
    return false;
  }
  
  if (mqttClient.connected()) {
    return true;
  }
  
  Serial.printf("Attempting MQTT connection to %s:%d...\n", mqttServer.c_str(), mqttPort);
  mqttClient.setServer(mqttServer.c_str(), mqttPort);
  mqttClient.setCallback(mqttCallback);
  
  String clientId = "lighthouse-esp32-" + String(random(0xffff), HEX);
  bool connected = false;
  
  if (mqttUsername.length() > 0 && mqttPassword.length() > 0) {
    connected = mqttClient.connect(clientId.c_str(), mqttUsername.c_str(), mqttPassword.c_str());
  } else {
    connected = mqttClient.connect(clientId.c_str());
  }
  
  if (connected) {
    Serial.println("MQTT connected");
    // Subscribe to command topics
    String cmdTopic = mqttTopic + "/command";
    mqttClient.subscribe(cmdTopic.c_str());
    Serial.printf("Subscribed to: %s\n", cmdTopic.c_str());
    
    // Subscribe to individual lighthouse command topics
    for (int i = 0; i < 2; i++) {
      String individualTopic = mqttTopic + "/lighthouse" + String(i) + "/command";
      mqttClient.subscribe(individualTopic.c_str());
      Serial.printf("Subscribed to: %s\n", individualTopic.c_str());
    }
    
    // Publish availability
    String availTopic = mqttTopic + "/availability";
    mqttClient.publish(availTopic.c_str(), "online", true);
  } else {
    Serial.printf("MQTT connection failed, rc=%d\n", mqttClient.state());
  }
  
  return connected;
}

void publishMqttStatus() {
  if (!mqttClient.connected()) return;
  
  // Publish lighthouse status for each discovered lighthouse
  for (int i = 0; i < lighthouseCount && i < 2; i++) { // Only publish for 2 masters
    String statusTopic = mqttTopic + "/lighthouse" + String(i) + "/status";
    String nameTopic = mqttTopic + "/lighthouse" + String(i) + "/name";
    
    // Publish status (simplified - in real implementation you'd track actual state)
    mqttClient.publish(statusTopic.c_str(), "unknown");
    
    // Publish name
    if (discoveredLighthouseIds[i] >= 0 && discoveredLighthouseIds[i] < 2) {
      String name = preferences.getString(("name_" + String(discoveredLighthouseIds[i])).c_str(), lighthouseNames[discoveredLighthouseIds[i]]);
      mqttClient.publish(nameTopic.c_str(), name.c_str());
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting SteamVR Lighthouse Controller...");

  // Initialize LED pin
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // Initialize buttons
  offButton.begin();
  onButton.begin();

  // Initialize BLE
  Serial.println("Initializing BLE...");
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */

  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(), false);
  pScan->setInterval(1349);
  pScan->setWindow(449);
  pScan->setActiveScan(true);

  // Initialize WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Setup web server
  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/rename", handleRename);
  server.on("/mqtt", handleMqttConfig);
  server.on("/mqtt-save", HTTP_POST, handleMqttSave);
  server.begin();
  Serial.println("Web server started");

  // Load and initialize MQTT configuration
  loadMqttConfig();
  if (mqttEnabled) {
    connectMqtt();
  }

  // Initial LED blink to show setup complete
  blinkLED(3, 200);
  
  Serial.println("Setup complete!");
  Serial.println("Web interface available at: http://" + WiFi.localIP().toString());
}

void loop() {
  server.handleClient();
  
  // Handle MQTT
  if (mqttEnabled) {
    if (!mqttClient.connected()) {
      unsigned long now = millis();
      if (now - lastMqttReconnectAttempt > 5000) { // Try to reconnect every 5 seconds
        lastMqttReconnectAttempt = now;
        if (connectMqtt()) {
          Serial.println("MQTT reconnected");
        }
      }
    } else {
      mqttClient.loop();
    }
  }
  
  // Handle button presses
  offButton.read();
  onButton.read();
  
  if (offButton.wasPressed() && currentCommand == NOTHING) {
    Serial.println("Off button pressed");
    startScanAndSetCommand(TURN_OFF);
  }
  
  if (onButton.wasPressed() && currentCommand == NOTHING) {
    Serial.println("On button pressed");
    startScanAndSetCommand(TURN_ON_PERM);
  }

  // Handle BLE operations
  if (readyToConnect && currentCommand != NOTHING) {
    readyToConnect = false;
    
    bool success = sendLighthouseCommands();
    
    if (success) {
      Serial.println("Commands sent successfully");
      blinkLED(2, 500); // Success: 2 slow blinks
      
      // Publish status update via MQTT
      if (mqttClient.connected()) {
        publishMqttStatus();
      }
    } else {
      Serial.println("Some commands failed");
      blinkLED(5, 100); // Error: 5 fast blinks
    }
    
    currentCommand = NOTHING;
    targetLighthouseIndex = -1; // Reset target
    digitalWrite(ledPin, LOW);
    
    // Cleanup
    auto clientList = NimBLEDevice::getClientList();
    for (auto client : *clientList) {
      NimBLEDevice::deleteClient(client);
    }
  }

  delay(10);
}
