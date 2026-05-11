/*
 * ============================================================
 * ESP32 LED STRIP CONTROLLER — WebSocket Sample (Exam Ready)
 * ============================================================
 *
 * WHAT THIS DOES:
 *   - Connects ESP32 to WiFi and a WebSocket server
 *   - Controls a WS2812B LED strip based on server commands
 *   - Sends button/joystick events back to the server
 *   - Runs non-blocking animations so WebSocket stays alive
 *
 * HARDWARE:
 *   - ESP32 DevKit
 *   - WS2812B LED Strip (30 LEDs, 5V external PSU)
 *   - Push button on GPIO 27 (active LOW, internal pull-up)
 *   - Optional: analog joystick VRX on GPIO 34, VRY on GPIO 35
 *
 * WIRING:
 *   Strip 5V  --> External 5V PSU
 *   Strip GND --> PSU GND  AND  ESP32 GND  (common ground!)
 *   Strip DIN --> GPIO 5   (add 300Ω resistor in series)
 *   BTN_PIN   --> GPIO 27  (other leg to GND)
 *
 * LIBRARIES (install via Arduino Library Manager):
 *   - Adafruit NeoPixel  by Adafruit
 *   - WebSocketsClient   by Markus Sattler
 *   - ArduinoJson        by Benoit Blanchon
 *
 * SERVER MESSAGES THIS CODE HANDLES:
 *   {"type":"joined",    "deviceId":"abc123"}
 *   {"type":"color",     "r":255, "g":0, "b":0}
 *   {"type":"effect",    "effect":"rainbow"}   // rainbow, wipe, blink, off, chase
 *   {"type":"brightness","value":100}
 *   {"type":"ping"}
 *
 * MESSAGES THIS CODE SENDS:
 *   {"type":"join",   "name":"LED_ESP32", "leds":30}
 *   {"type":"button", "pressed":true}
 *   {"type":"pong"}
 *   {"type":"status", "connected":true, "brightness":50, "leds":30}
 * ============================================================
 */

// ============================================================
// LIBRARIES
// ============================================================
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// ============================================================
// CONFIGURATION — CHANGE THESE FOR YOUR SETUP
// ============================================================

// WiFi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// WebSocket server
const char* SERVER_HOST = "192.168.1.100";  // Server IP
const int   SERVER_PORT = 8080;             // Server port
// NOTE: If server uses wss:// (TLS, like port 443), change connectWebSocket()
//       to use webSocket.beginSSL() instead of webSocket.begin()

// Device identity
const char* DEVICE_NAME = "LED_ESP32";

// ============================================================
// PIN DEFINITIONS
// ============================================================
#define LED_PIN     5   // Data line to strip (add 300Ω series resistor)
#define LED_COUNT  30   // Number of LEDs in your strip
#define BTN_PIN    27   // Push button (active LOW with INPUT_PULLUP)
#define JOY_VRX    34   // Joystick X axis (ADC1 — safe with WiFi)
#define JOY_VRY    35   // Joystick Y axis (ADC1 — safe with WiFi)

// ============================================================
// GLOBAL OBJECTS
// ============================================================
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
WebSocketsClient  webSocket;

// ============================================================
// STATE VARIABLES
// ============================================================

// Connection state
bool     isConnected = false;
String   deviceId    = "";

// Button state (debounced)
bool     btnState         = false;
bool     prevBtnState     = false;
unsigned long lastDebounce = 0;
const int DEBOUNCE_MS     = 50;

// Current LED settings
int currentR = 0, currentG = 0, currentB = 0;
int currentBrightness = 50;
String currentEffect = "off";   // tracks which effect is active

// Non-blocking animation timing
unsigned long lastAnimFrame = 0;
const unsigned long ANIM_INTERVAL = 20; // ms between animation frames
static uint8_t animHue = 0;            // used by rainbow, chase, etc.
static int     animStep = 0;           // used by wipe, chase

// Non-blocking button check timing
unsigned long lastBtnCheck = 0;
const int BTN_CHECK_INTERVAL = 10; // ms

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP32 LED WebSocket Controller ===");

    // Initialize button pin
    pinMode(BTN_PIN, INPUT_PULLUP);

    // Initialize LED strip
    strip.begin();
    strip.setBrightness(currentBrightness);
    strip.clear();
    strip.show();   // All LEDs off at start
    Serial.println("[LED] Strip initialized.");

    // Startup animation — lets you verify wiring before WiFi connects
    startupBlink();

    // Connect WiFi, then WebSocket
    connectWiFi();
    connectWebSocket();
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
    // ── 1. WebSocket MUST be first ──────────────────────────
    webSocket.loop();

    // ── 2. Check button (non-blocking) ──────────────────────
    unsigned long now = millis();
    if (now - lastBtnCheck >= BTN_CHECK_INTERVAL) {
        lastBtnCheck = now;
        checkButton();
    }

    // ── 3. Run animation frame (non-blocking) ───────────────
    // Only runs effects that need continuous updates
    if (now - lastAnimFrame >= ANIM_INTERVAL) {
        lastAnimFrame = now;
        runAnimationFrame();
    }

    // ── 4. Auto-reconnect WiFi if dropped ───────────────────
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Connection lost, reconnecting...");
        connectWiFi();
        connectWebSocket();
    }
}

// ============================================================
// WIFI CONNECTION
// ============================================================
void connectWiFi() {
    Serial.print("[WIFI] Connecting to: ");
    Serial.println(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WIFI] Connected!");
        Serial.print("[WIFI] IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[WIFI] Failed to connect — restarting...");
        delay(2000);
        ESP.restart();
    }
}

// ============================================================
// WEBSOCKET CONNECTION
// ============================================================
void connectWebSocket() {
    Serial.print("[WS] Connecting to server: ");
    Serial.print(SERVER_HOST);
    Serial.print(":");
    Serial.println(SERVER_PORT);

    // Use begin() for ws://, beginSSL() for wss://
    webSocket.begin(SERVER_HOST, SERVER_PORT, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);   // auto-reconnect every 5s
}

// ============================================================
// WEBSOCKET EVENT HANDLER
// ============================================================
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {

        case WStype_CONNECTED:
            Serial.println("[WS] Connected to server!");
            isConnected = true;
            sendJoinMessage();      // announce ourselves right away
            break;

        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected!");
            isConnected = false;
            deviceId = "";
            break;

        case WStype_TEXT:
            // payload is the raw JSON string from server
            handleServerMessage((char*)payload);
            break;

        case WStype_ERROR:
            Serial.println("[WS] WebSocket error!");
            break;

        case WStype_PING:
        case WStype_PONG:
            // Library handles keep-alive automatically — nothing needed here
            break;
    }
}

// ============================================================
// SEND: JOIN MESSAGE
// ============================================================
void sendJoinMessage() {
    StaticJsonDocument<128> doc;
    doc["type"] = "join";
    doc["name"] = DEVICE_NAME;
    doc["leds"] = LED_COUNT;        // tell server how many LEDs we have

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);

    Serial.println("[WS] Sent join message.");
}

// ============================================================
// SEND: STATUS UPDATE
// ============================================================
void sendStatus() {
    if (!isConnected) return;

    StaticJsonDocument<128> doc;
    doc["type"]       = "status";
    doc["connected"]  = true;
    doc["brightness"] = currentBrightness;
    doc["leds"]       = LED_COUNT;
    doc["effect"]     = currentEffect;

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);
}

// ============================================================
// SEND: BUTTON PRESS
// ============================================================
void sendButtonEvent(bool pressed) {
    if (!isConnected) return;

    StaticJsonDocument<64> doc;
    doc["type"]    = "button";
    doc["pressed"] = pressed;

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);

    Serial.print("[WS] Sent button event: ");
    Serial.println(pressed ? "PRESSED" : "RELEASED");
}

// ============================================================
// RECEIVE: HANDLE SERVER MESSAGES
// ============================================================
void handleServerMessage(char* payload) {
    Serial.print("[WS] Received: ");
    Serial.println(payload);

    // Parse JSON — buffer must be large enough for the message!
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.print("[JSON] Parse error: ");
        Serial.println(error.c_str());
        return;
    }

    // Get the "type" field — use strcmp() for const char* comparison!
    const char* type = doc["type"];

    // ── "joined" — server confirmed our join ────────────────
    if (strcmp(type, "joined") == 0) {
        deviceId = doc["deviceId"].as<String>();
        Serial.print("[GAME] Joined! Device ID: ");
        Serial.println(deviceId);

        // Flash green 3 times to confirm connection
        for (int i = 0; i < 3; i++) {
            strip.fill(strip.Color(0, 255, 0));
            strip.show();
            delay(150);
            strip.clear();
            strip.show();
            delay(150);
        }
        currentEffect = "off";
        sendStatus();
    }

    // ── "color" — set strip to a solid RGB color ────────────
    else if (strcmp(type, "color") == 0) {
        currentR = doc["r"];    // int, 0-255
        currentG = doc["g"];
        currentB = doc["b"];
        currentEffect = "solid";

        strip.fill(strip.Color(currentR, currentG, currentB));
        strip.show();

        Serial.printf("[LED] Color set: R=%d G=%d B=%d\n", currentR, currentG, currentB);
    }

    // ── "brightness" — change global brightness ─────────────
    else if (strcmp(type, "brightness") == 0) {
        currentBrightness = doc["value"];   // 0-255
        currentBrightness = constrain(currentBrightness, 0, 255);
        strip.setBrightness(currentBrightness);
        strip.show();   // re-push with new brightness

        Serial.printf("[LED] Brightness: %d\n", currentBrightness);
    }

    // ── "effect" — trigger an animation ─────────────────────
    else if (strcmp(type, "effect") == 0) {
        const char* effect = doc["effect"];
        currentEffect = String(effect);
        animStep = 0;       // reset animation state
        animHue  = 0;

        Serial.print("[LED] Effect: ");
        Serial.println(effect);

        // Blocking effects run here entirely
        if (strcmp(effect, "wipe") == 0) {
            // Color wipe — uses r/g/b from message, or defaults to white
            int r = doc["r"] | 255;
            int g = doc["g"] | 255;
            int b = doc["b"] | 0;
            colorWipe(strip.Color(r, g, b), 40);
            currentEffect = "solid";
        }
        else if (strcmp(effect, "blink") == 0) {
            // Blink 5 times with given color
            int r = doc["r"] | 255;
            int g = doc["g"] | 0;
            int b = doc["b"] | 0;
            blinkAll(strip.Color(r, g, b), 5, 200);
            currentEffect = "solid";
        }
        // Non-blocking effects (rainbow, chase) are handled in runAnimationFrame()
    }

    // ── "off" — turn strip off ──────────────────────────────
    else if (strcmp(type, "off") == 0) {
        currentEffect = "off";
        strip.clear();
        strip.show();
        Serial.println("[LED] Strip off.");
    }

    // ── "ping" — server health check, reply with pong ───────
    else if (strcmp(type, "ping") == 0) {
        StaticJsonDocument<32> pong;
        pong["type"] = "pong";
        String msg;
        serializeJson(pong, msg);
        webSocket.sendTXT(msg);
    }

    else {
        Serial.print("[WS] Unknown message type: ");
        Serial.println(type);
    }
}

// ============================================================
// BUTTON HANDLING — DEBOUNCED
// ============================================================
void checkButton() {
    bool reading = !digitalRead(BTN_PIN);  // LOW = pressed (INPUT_PULLUP)

    // Debounce: only accept change if stable for DEBOUNCE_MS
    if (reading != btnState) {
        if (millis() - lastDebounce > DEBOUNCE_MS) {
            lastDebounce = millis();
            btnState = reading;
        }
    }

    // Detect rising edge (just pressed)
    if (btnState && !prevBtnState) {
        Serial.println("[BTN] Button PRESSED");
        sendButtonEvent(true);

        // Toggle effect on button press as local demo
        if (currentEffect == "off" || currentEffect == "solid") {
            currentEffect = "rainbow";
        } else {
            currentEffect = "off";
            strip.clear();
            strip.show();
        }
    }

    // Detect falling edge (just released)
    if (!btnState && prevBtnState) {
        Serial.println("[BTN] Button RELEASED");
        sendButtonEvent(false);
    }

    prevBtnState = btnState;
}

// ============================================================
// NON-BLOCKING ANIMATION FRAME
// Called every ANIM_INTERVAL ms from loop()
// Only runs for effects that need continuous updates.
// ============================================================
void runAnimationFrame() {
    if (currentEffect == "rainbow") {
        // Smooth rotating rainbow across all LEDs
        for (int i = 0; i < strip.numPixels(); i++) {
            int pixelHue = animHue * 256 + (i * 65536L / strip.numPixels());
            strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
        }
        strip.show();
        animHue++;  // uint8_t wraps at 255 automatically
    }
    else if (currentEffect == "chase") {
        // Theater chase — every 3rd LED lights up and shifts
        strip.clear();
        for (int i = animStep; i < strip.numPixels(); i += 3) {
            strip.setPixelColor(i, strip.Color(currentR, currentG, currentB));
        }
        strip.show();
        animStep = (animStep + 1) % 3;
    }
    // "off", "solid", "wipe", "blink" — no continuous updates needed
}

// ============================================================
// BLOCKING EFFECTS (short enough not to matter)
// ============================================================

// Light up LEDs one by one
void colorWipe(uint32_t color, int waitMs) {
    for (int i = 0; i < strip.numPixels(); i++) {
        strip.setPixelColor(i, color);
        strip.show();
        delay(waitMs);
    }
}

// Blink all LEDs N times
void blinkAll(uint32_t color, int times, int waitMs) {
    for (int t = 0; t < times; t++) {
        strip.fill(color);
        strip.show();
        delay(waitMs);
        strip.clear();
        strip.show();
        delay(waitMs);
    }
}

// 3 fast flashes at startup to confirm strip is wired correctly
void startupBlink() {
    for (int i = 0; i < 3; i++) {
        strip.fill(strip.Color(0, 0, 50));   // dim blue
        strip.show();
        delay(100);
        strip.clear();
        strip.show();
        delay(100);
    }
    Serial.println("[LED] Startup blink done.");
}

// ============================================================
// END OF FILE
// ============================================================
