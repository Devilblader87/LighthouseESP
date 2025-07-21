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

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "JC_Button.h"
#include <WiFi.h>
#include <WebServer.h>

// For Version 1 (HTC) Base Stations:

// You can place up to CONFIG_BT_NIMBLE_MAX_CONNECTIONS amount of IDs here.
// (ESP32 max is 9) Find this on the back of your Base Station. Technically you
// only need to enter the B station ID, but C will look around for B for a while
// before shutting down, so I personally put both in :) This is required to turn
// the Base Station off immediately, and as such, this app is configured so if
// you want this to even turn it ON, you need the ID in here.
// My Base Stations for example: "7F35E5C5", "034996AB"
const char* lighthouseHTCIDs[] = {"7F35E5C5", "034996AB"};

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

const char* ssid = "YOUR_WIFI_SSID";        // TODO: set your WiFi SSID
const char* password = "YOUR_WIFI_PASSWORD"; // TODO: set your WiFi password

WebServer server(80);

// The remote service we wish to connect to.
// static NimBLEUUID serviceUUIDHTC("0000cb00-0000-1000-8000-00805f9b34fb");
// ^ V1 Service long UUID
static NimBLEUUID serviceUUIDHTC("CB00");
// The characteristic of the remote service we are interested in.
// static NimBLEUUID
// characteristicUUIDHTC("0000cb01-0000-1000-8000-00805f9b34fb"); ^ V1
// Characteristic long UUID
static NimBLEUUID characteristicUUIDHTC("CB01");

static NimBLEUUID serviceUUIDV2("00001523-1212-efde-1523-785feabcd124");
static NimBLEUUID characteristicUUIDV2("00001525-1212-efde-1523-785feabcd124");

enum { NOTHING = 0, TURN_ON_PERM = 1, TURN_OFF = 2 };

#define MAX_DISCOVERABLE_LH CONFIG_BT_NIMBLE_MAX_CONNECTIONS

static NimBLEAdvertisedDevice* discoveredLighthouses[MAX_DISCOVERABLE_LH];

uint8_t discoveredLighthouseVersions[MAX_DISCOVERABLE_LH];

uint8_t currentCommand = NOTHING;

int lighthouseCount = 0;

void scanEndedCB(NimBLEScanResults results);

static bool readyToConnect = false;
static uint32_t scanTime = 5; /** 0 = scan forever. In seconds */

void startScanAndSetCommand(uint8_t command) {
  digitalWrite(ledPin, HIGH);
  lighthouseCount = 0;
  for (uint8_t i = 0; i < MAX_DISCOVERABLE_LH; i++) {
    discoveredLighthouses[i] = nullptr;
    discoveredLighthouseVersions[i] = 0;
  }
  NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
  currentCommand = command;
}

void handleRoot() {
  String html = "<html><head><title>Lighthouse Controller</title></head><body>";
  html += "<h1>SteamVR Lighthouse Controller</h1>";
  html += "<p><a href='/on'><button style='font-size:20px;padding:10px'>Turn ON</button></a></p>";
  html += "<p><a href='/off'><button style='font-size:20px;padding:10px'>Turn OFF</button></a></p>";
  html += "<p>Status: ";
  if (currentCommand == NOTHING) {
    html += "Ready";
  } else if (currentCommand == TURN_ON_PERM) {
    html += "Turning ON...";
  } else if (currentCommand == TURN_OFF) {
    html += "Turning OFF...";
  }
  html += "</p>";
  html += "<p>Discovered Lighthouses: " + String(lighthouseCount) + "</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleOn() {
  if (currentCommand == NOTHING) {
    startScanAndSetCommand(TURN_ON_PERM);
    server.send(200, "text/html", "<html><body><h1>Turning ON Lighthouses...</h1><p><a href='/'>Back</a></p></body></html>");
  } else {
    server.send(200, "text/html", "<html><body><h1>Command already in progress</h1><p><a href='/'>Back</a></p></body></html>");
  }
}

void handleOff() {
  if (currentCommand == NOTHING) {
    startScanAndSetCommand(TURN_OFF);
    server.send(200, "text/html", "<html><body><h1>Turning OFF Lighthouses...</h1><p><a href='/'>Back</a></p></body></html>");
  } else {
    server.send(200, "text/html", "<html><body><h1>Command already in progress</h1><p><a href='/'>Back</a></p></body></html>");
  }
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
      
      for (int i = 0; i < sizeof(lighthouseHTCIDs) / sizeof(lighthouseHTCIDs[0]); i++) {
        if (advertisedName.indexOf(lighthouseHTCIDs[i]) >= 0) {
          Serial.printf("Found V1 Lighthouse: %s\n", advertisedName.c_str());
          discoveredLighthouses[lighthouseCount] = advertisedDevice;
          discoveredLighthouseVersions[lighthouseCount] = 1;
          lighthouseCount++;
          break;
        }
      }
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
        lighthouseCount++;
      }
    }
  }
};

bool sendLighthouseCommands() {
  if (NimBLEDevice::getCreatedClientCount()) {
    Serial.println("Deleting clients");
    NimBLEDevice::deleteAllClients();
  }

  if (lighthouseCount == 0) {
    Serial.println("No lighthouses found!");
    return false;
  }

  bool success = true;

  for (int i = 0; i < lighthouseCount; i++) {
    if (discoveredLighthouses[i] == nullptr) continue;

    if (NimBLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) {
      Serial.println("Max clients reached");
      break;
    }

    NimBLEClient* pClient = nullptr;

    if (NimBLEDevice::getCreatedClientCount()) {
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
      if (NimBLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) {
        Serial.println("Max clients reached - Unable to create client");
        continue;
      }

      pClient = NimBLEDevice::createClient();
      Serial.println("New client created");

      pClient->setClientCallbacks(&clientCB, false);
      pClient->setConnectionParams(12, 12, 0, 51);
      pClient->setConnectTimeout(5);

      if (!pClient->connect(discoveredLighthouses[i])) {
        NimBLEDevice::deleteClient(pClient);
        Serial.println("Failed to connect");
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
      pSvc = pClient->getService(serviceUUIDHTC);
      if (pSvc) {
        pChr = pSvc->getCharacteristic(characteristicUUIDHTC);
        if (pChr) {
          if (pChr->canWrite()) {
            uint8_t command = (currentCommand == TURN_ON_PERM) ? 0x01 : 0x00;
            if (pChr->writeValue(&command, 1)) {
              Serial.printf("Sent V1 command: %02X\n", command);
            } else {
              Serial.println("Failed to send V1 command");
              success = false;
            }
          }
        }
      }
    }
    // Handle V2 Base Stations
    else if (discoveredLighthouseVersions[i] == 2) {
      pSvc = pClient->getService(serviceUUIDV2);
      if (pSvc) {
        pChr = pSvc->getCharacteristic(characteristicUUIDV2);
        if (pChr) {
          if (pChr->canWrite()) {
            uint8_t command = (currentCommand == TURN_ON_PERM) ? 0x01 : 0x00;
            if (pChr->writeValue(&command, 1)) {
              Serial.printf("Sent V2 command: %02X\n", command);
            } else {
              Serial.println("Failed to send V2 command");
              success = false;
            }
          }
        }
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
  server.begin();
  Serial.println("Web server started");

  // Initial LED blink to show setup complete
  blinkLED(3, 200);
  
  Serial.println("Setup complete!");
  Serial.println("Web interface available at: http://" + WiFi.localIP().toString());
}

void loop() {
  server.handleClient();
  
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
    } else {
      Serial.println("Some commands failed");
      blinkLED(5, 100); // Error: 5 fast blinks
    }
    
    currentCommand = NOTHING;
    digitalWrite(ledPin, LOW);
    
    // Cleanup
    if (NimBLEDevice::getCreatedClientCount()) {
      NimBLEDevice::deleteAllClients();
    }
  }

  delay(10);
}
