/*
 * WebSocket Controlled LED
 *
 * Simple ESP32 WebSocket server to control LEDs remotely.
 * Receives JSON commands to turn LEDs on/off and adjust brightness.
 *
 * Hardware Requirements:
 * - ESP32 DevKit
 * - LED with resistor (220-1kΩ recommended)
 * - LED connected to GPIO 5 (cathode to GND)
 *
 * Libraries Required:
 * - WiFi (built-in)
 * - WebSocketsServer by Markus Sattler
 * - ArduinoJson by Benoit Blanchon
 *
 * WebSocket Commands (send as JSON):
 * {"type": "led", "state": true}        - Turn LED on
 * {"type": "led", "state": false}       - Turn LED off
 * {"type": "led", "brightness": 128}    - Set brightness (0-255)
 * {"type": "status"}                    - Request current LED status
 */

#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ============================================
// CONFIGURATION
// ============================================

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

const int LED_PIN = 5;              // GPIO 5 for LED
const int WS_PORT = 81;             // WebSocket server port
const int STATUS_LED = 2;           // Built-in LED for connection status

// ============================================
// GLOBAL VARIABLES
// ============================================

WebSocketsServer webSocket = WebSocketsServer(WS_PORT);

bool ledState = false;
int ledBrightness = 255;            // 0-255 for PWM
uint8_t connectedClients = 0;

// ============================================
// SETUP
// ============================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== ESP32 WebSocket Controlled LED ===\n");

    // Initialize pins
    pinMode(LED_PIN, OUTPUT);
    pinMode(STATUS_LED, OUTPUT);
    
    digitalWrite(LED_PIN, LOW);
    digitalWrite(STATUS_LED, LOW);

    // Configure PWM for LED brightness control
    ledcSetup(0, 5000, 8);          // Channel 0, 5kHz frequency, 8-bit resolution
    ledcAttachPin(LED_PIN, 0);
    ledcWrite(0, 0);                 // Start with LED off

    // Connect to WiFi
    connectToWiFi();

    // Start WebSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    
    Serial.print("WebSocket server started on port ");
    Serial.println(WS_PORT);
    Serial.print("Server IP: ");
    Serial.println(WiFi.localIP());
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
    webSocket.loop();

    // Blink status LED
    if (connectedClients > 0) {
        digitalWrite(STATUS_LED, (millis() / 500) % 2);  // Slow blink when connected
    } else {
        digitalWrite(STATUS_LED, (millis() / 100) % 2);  // Fast blink when idle
    }
}

// ============================================
// WIFI CONNECTION
// ============================================

void connectToWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("WiFi connection failed!");
    }
}

// ============================================
// WEBSOCKET EVENT HANDLER
// ============================================

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WS] Client %u disconnected\n", num);
            connectedClients--;
            break;

        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("[WS] Client %u connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
            connectedClients++;
            sendStatus(num);  // Send current status to new client
            break;
        }

        case WStype_TEXT:
            handleMessage(num, payload, length);
            break;

        case WStype_ERROR:
            Serial.printf("[WS] Error on client %u\n", num);
            break;

        case WStype_PING:
        case WStype_PONG:
            break;
    }
}

// ============================================
// MESSAGE HANDLING
// ============================================

void handleMessage(uint8_t clientNum, uint8_t * payload, size_t length) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        return;
    }

    const char* type = doc["type"];

    if (strcmp(type, "led") == 0) {
        // Handle LED control commands
        if (doc.containsKey("state")) {
            ledState = doc["state"];
            updateLED();
            Serial.printf("LED state: %s\n", ledState ? "ON" : "OFF");
        }

        if (doc.containsKey("brightness")) {
            ledBrightness = doc["brightness"];
            ledBrightness = constrain(ledBrightness, 0, 255);
            updateLED();
            Serial.printf("LED brightness: %d\n", ledBrightness);
        }

        // Broadcast new status to all clients
        broadcastStatus();
    }
    else if (strcmp(type, "status") == 0) {
        // Send current status to requesting client
        sendStatus(clientNum);
    }
    else {
        Serial.printf("Unknown command type: %s\n", type);
    }
}

// ============================================
// LED CONTROL
// ============================================

void updateLED() {
    if (ledState) {
        ledcWrite(0, ledBrightness);  // Set brightness
    } else {
        ledcWrite(0, 0);               // Turn off
    }
}

// ============================================
// STATUS COMMUNICATION
// ============================================

void sendStatus(uint8_t clientNum) {
    StaticJsonDocument<128> doc;
    doc["type"] = "status";
    doc["led"] = ledState;
    doc["brightness"] = ledBrightness;
    doc["clients"] = connectedClients;

    String json;
    serializeJson(doc, json);
    webSocket.sendTXT(clientNum, json);

    Serial.printf("Status sent to client %u\n", clientNum);
}

void broadcastStatus() {
    StaticJsonDocument<128> doc;
    doc["type"] = "status";
    doc["led"] = ledState;
    doc["brightness"] = ledBrightness;
    doc["clients"] = connectedClients;

    String json;
    serializeJson(doc, json);
    webSocket.broadcastTXT(json);

    Serial.println("Status broadcasted to all clients");
}
