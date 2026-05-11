/*
 * Tank 1990 - ESP32 Controller
 *
 * This code runs on an ESP32 device to control a tank in the
 * Tank 1990 multiplayer game via WebSocket connection.
 *
 * Hardware Requirements:
 * - ESP32 DevKit or similar
 * - 2-axis analog joystick module (e.g. KY-023): VRX, VRY, SW (push)
 *
 * Joystick Wiring (typical module — adjust if your board differs):
 * - VRX -> GPIO 34 (ADC1_CH6)
 * - VRY -> GPIO 35 (ADC1_CH7)
 * - SW  -> GPIO 27 (push-to-fire, active LOW with internal pull-up)
 * - +   -> 3.3V
 * - GND -> GND
 *
 * (Replaced: 4 Navigation buttons UP/DOWN/LEFT/RIGHT + separate fire button)
 *
 * Libraries Required:
 * - WiFi (built-in)
 * - WebSocketsClient by Markus Sattler
 * - ArduinoJson by Benoit Blanchon
 */

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ============================================
// CONFIGURATION - MODIFY THESE VALUES
// ============================================

// WiFi Configuration
const char* ssid = "TP-Link_79C8";
const char* password = "84372871";

// Game Server Configuration
const char* SERVER_HOST = "15.235.204.158";
const int SERVER_PORT = 443 ;

// Tank Configuration (Fixed for this device)
const char* TANK_NAME = "Error404";
const char* PRIMARY_COLOR = "##FFD700";    // Gold
const char* SECONDARY_COLOR = "#006400";  // Dark Green
const char* AVATAR_URL = "https://image2url.com/r2/default/images/1774230520830-18afa016-9c10-44d5-a560-3e93da03f55e.webp"; // Optional avatar URL

// ============================================
// PIN DEFINITIONS
// ============================================

// #define BTN_UP    32
// #define BTN_DOWN  33
// #define BTN_LEFT  25
// #define BTN_RIGHT 26
// #define BTN_FIRE  27

// 2-axis joystick (analog axes on ADC-capable pins; SW = push button)
#define JOY_VRX   3
#define JOY_VRY   4
#define JOY_SW    2

#define LED_BUILTIN 1  // Built-in LED for status

// Joystick: smoothing & thresholds (tune DEAD_ZONE if drift occurs)
const float JOY_SMOOTH_ALPHA = 0.22f;   // EMA weight for new sample (lower = smoother, more lag)
const int JOY_DEAD_ZONE = 380;          // ADC units from calibrated center before a direction is "on"
const int JOY_CALIB_SAMPLES = 48;       // samples averaged at startup for center (keep stick centered)

// Serial: 115200 baud; optional periodic joystick lines (set to 0 to disable)
const unsigned long SERIAL_JOY_DEBUG_MS = 200;

// ============================================
// GLOBAL VARIABLES
// ============================================

WebSocketsClient webSocket;

// Smoothed joystick ADC values (after exponential moving average)
float joySmoothX = 2048.0f;
float joySmoothY = 2048.0f;
int joyCenterX = 2048;
int joyCenterY = 2048;

// Button states
bool btnUp = false;
bool btnDown = false;
bool btnLeft = false;
bool btnRight = false;
bool btnFire = false;

// Previous button states (for edge detection)
bool prevBtnUp = false;
bool prevBtnDown = false;
bool prevBtnLeft = false;
bool prevBtnRight = false;
bool prevBtnFire = false;

// Connection state
bool isConnected = false;
String playerId = "";

// Game stats
int health = 100;
int maxHealth = 100;
int score = 0;
int kills = 0;
int deaths = 0;
bool spawnProtection = false;

// Timing
unsigned long lastButtonCheck = 0;
const int BUTTON_CHECK_INTERVAL = 10;    // ms

// Debounce (fire / SW only; movement comes from smoothed analog)
// unsigned long lastDebounceTime[5] = {0, 0, 0, 0, 0};
// const int DEBOUNCE_DELAY = 50; // ms
unsigned long lastFireDebounceTime = 0;
const int DEBOUNCE_DELAY = 50; // ms

unsigned long lastSerialJoyDebug = 0;

// ============================================
// SETUP
// ============================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Tank 1990 ESP32 Controller ===");

    // Initialize pins
    // pinMode(BTN_UP, INPUT_PULLUP);
    // pinMode(BTN_DOWN, INPUT_PULLUP);
    // pinMode(BTN_LEFT, INPUT_PULLUP);
    // pinMode(BTN_RIGHT, INPUT_PULLUP);
    // pinMode(BTN_FIRE, INPUT_PULLUP);
    pinMode(JOY_SW, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);

    analogReadResolution(12);

    // Status LED off initially
    digitalWrite(LED_BUILTIN, LOW);

    calibrateJoystick();

    // Connect to WiFi
    connectWiFi();

    // Connect to game server
    connectWebSocket();
}

// Sample joystick center — keep stick centered and still during boot
void calibrateJoystick() {
    long sumX = 0;
    long sumY = 0;
    for (int i = 0; i < JOY_CALIB_SAMPLES; i++) {
        sumX += analogRead(JOY_VRX);
        sumY += analogRead(JOY_VRY);
        delay(5);
    }
    joyCenterX = sumX / JOY_CALIB_SAMPLES;
    joyCenterY = sumY / JOY_CALIB_SAMPLES;
    joySmoothX = (float)joyCenterX;
    joySmoothY = (float)joyCenterY;
    Serial.print("[JOY] Calibrated center X=");
    Serial.print(joyCenterX);
    Serial.print(" Y=");
    Serial.println(joyCenterY);
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
    unsigned long currentTime = millis();

    // Joystick first: always runs and can print [JOY] lines even when WebSocket/game is not connected
    if (currentTime - lastButtonCheck >= BUTTON_CHECK_INTERVAL) {
        lastButtonCheck = currentTime;
        checkButtons();
    }

    // Handle WebSocket events (after local input so bench testing works without a server)
    webSocket.loop();

    // Blink LED when connected
    if (isConnected) {
        digitalWrite(LED_BUILTIN, (millis() / 500) % 2);
    } else {
        digitalWrite(LED_BUILTIN, (millis() / 100) % 2); // Fast blink when disconnected
    }
}

// ============================================
// WIFI CONNECTION
// ============================================

void connectWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed!");
        delay(3000);
        ESP.restart();
    }
}

// ============================================
// WEBSOCKET CONNECTION
// ============================================

void connectWebSocket() {
    Serial.print("Connecting to game server: ");
    Serial.print(SERVER_HOST);
    Serial.print(":");
    Serial.println(SERVER_PORT);

    webSocket.begin(SERVER_HOST, SERVER_PORT, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected!");
            isConnected = false;
            playerId = "";
            break;

        case WStype_CONNECTED:
            Serial.println("[WS] Connected to server!");
            sendJoinMessage();
            break;

        case WStype_TEXT:
            handleServerMessage((char*)payload);
            break;

        case WStype_ERROR:
            Serial.println("[WS] Error!");
            break;

        case WStype_PING:
        case WStype_PONG:
            break;
    }
}

void sendJoinMessage() {
    StaticJsonDocument<256> doc;
    doc["type"] = "esp32Join";  // Use esp32Join for ESP32 devices
    doc["name"] = TANK_NAME;
    doc["primaryColor"] = PRIMARY_COLOR;
    doc["secondaryColor"] = SECONDARY_COLOR;
    doc["avatarUrl"] = AVATAR_URL;

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);

    Serial.println("[WS] Sent esp32Join message");
}

void handleServerMessage(char* payload) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.print("[JSON] Parse error: ");
        Serial.println(error.c_str());
        return;
    }

    const char* type = doc["type"];

    if (strcmp(type, "joined") == 0) {
        isConnected = true;
        playerId = doc["playerId"].as<String>();
        Serial.print("[GAME] Joined! Player ID: ");
        Serial.println(playerId);
    }
    else if (strcmp(type, "gameState") == 0) {
        // Find our tank in the game state
        JsonArray tanks = doc["tanks"];
        for (JsonObject tank : tanks) {
            if (tank["id"].as<String>() == playerId) {
                health = tank["hp"];
                maxHealth = tank["maxHp"];
                score = tank["score"];
                kills = tank["kills"];
                deaths = tank["deaths"];
                spawnProtection = tank["spawnProtection"];
                break;
            }
        }
    }
    else if (strcmp(type, "tankDeath") == 0) {
        if (doc["tankId"].as<String>() == playerId) {
            Serial.println("[GAME] You were destroyed!");
        }
    }
    else if (strcmp(type, "powerupCollect") == 0) {
        if (doc["tankId"].as<String>() == playerId) {
            const char* powerupType = doc["powerupType"];
            Serial.print("[GAME] Collected powerup: ");
            Serial.println(powerupType);
        }
    }
    else if (strcmp(type, "kicked") == 0) {
        Serial.println("[GAME] You have been kicked!");
        isConnected = false;
    }
    else if (strcmp(type, "banned") == 0) {
        Serial.println("[GAME] You have been banned!");
        isConnected = false;
    }
}

// ============================================
// BUTTON / JOYSTICK HANDLING
// ============================================

// Throttled Serial line: not gated by isConnected — prints whenever SERIAL_JOY_DEBUG_MS > 0
static void printJoystickSerialIfDue(int rawX, int rawY, unsigned long currentTime) {
    if (SERIAL_JOY_DEBUG_MS == 0) {
        return;
    }
    if (currentTime - lastSerialJoyDebug < SERIAL_JOY_DEBUG_MS) {
        return;
    }
    lastSerialJoyDebug = currentTime;
    Serial.print("[JOY] ws=");
    Serial.print(isConnected ? 1 : 0);
    Serial.print(" raw X=");
    Serial.print(rawX);
    Serial.print(" Y=");
    Serial.print(rawY);
    Serial.print(" | sm X=");
    Serial.print((int)joySmoothX);
    Serial.print(" Y=");
    Serial.print((int)joySmoothY);
    Serial.print(" | left=");
    Serial.print(btnLeft ? 1 : 0);
    Serial.print(" right=");
    Serial.print(btnRight ? 1 : 0);
    Serial.print(" up=");
    Serial.print(btnUp ? 1 : 0);
    Serial.print(" down=");
    Serial.print(btnDown ? 1 : 0);
    Serial.print(" fire=");
    Serial.println(btnFire ? 1 : 0);
}

void checkButtons() {
    unsigned long currentTime = millis();

    // --- Replaced: digital directional buttons + raw reads ---
    // bool newUp = !digitalRead(BTN_UP);
    // bool newDown = !digitalRead(BTN_DOWN);
    // bool newLeft = !digitalRead(BTN_LEFT);
    // bool newRight = !digitalRead(BTN_RIGHT);
    // bool newFire = !digitalRead(BTN_FIRE);

    int rawX = analogRead(JOY_VRX);
    int rawY = analogRead(JOY_VRY);
    joySmoothX = JOY_SMOOTH_ALPHA * (float)rawX + (1.0f - JOY_SMOOTH_ALPHA) * joySmoothX;
    joySmoothY = JOY_SMOOTH_ALPHA * (float)rawY + (1.0f - JOY_SMOOTH_ALPHA) * joySmoothY;

    float dx = joySmoothX - (float)joyCenterX;
    float dy = joySmoothY - (float)joyCenterY;

    bool newUp = dy > (float)JOY_DEAD_ZONE;
    bool newDown = dy < -(float)JOY_DEAD_ZONE;
    bool newLeft = dx < -(float)JOY_DEAD_ZONE;
    bool newRight = dx > (float)JOY_DEAD_ZONE;

    btnUp = newUp;
    btnDown = newDown;
    btnLeft = newLeft;
    btnRight = newRight;

    // Debounce (directional): was per-button; analog + smoothing replaces that for movement
    // if (newUp != btnUp && (currentTime - lastDebounceTime[0] > DEBOUNCE_DELAY)) {
    //     lastDebounceTime[0] = currentTime;
    //     btnUp = newUp;
    // }
    // if (newDown != btnDown && (currentTime - lastDebounceTime[1] > DEBOUNCE_DELAY)) {
    //     lastDebounceTime[1] = currentTime;
    //     btnDown = newDown;
    // }
    // if (newLeft != btnLeft && (currentTime - lastDebounceTime[2] > DEBOUNCE_DELAY)) {
    //     lastDebounceTime[2] = currentTime;
    //     btnLeft = newLeft;
    // }
    // if (newRight != btnRight && (currentTime - lastDebounceTime[3] > DEBOUNCE_DELAY)) {
    //     lastDebounceTime[3] = currentTime;
    //     btnRight = newRight;
    // }
    // if (newFire != btnFire && (currentTime - lastDebounceTime[4] > DEBOUNCE_DELAY)) {
    //     lastDebounceTime[4] = currentTime;
    //     btnFire = newFire;
    // }

    bool rawFireDown = !digitalRead(JOY_SW);
    if (rawFireDown != btnFire && (currentTime - lastFireDebounceTime > DEBOUNCE_DELAY)) {
        lastFireDebounceTime = currentTime;
        btnFire = rawFireDown;
    }

    // Check for state changes
    bool movementChanged = (btnUp != prevBtnUp) || (btnDown != prevBtnDown) ||
                           (btnLeft != prevBtnLeft) || (btnRight != prevBtnRight);

    bool firePressed = (btnFire && !prevBtnFire);

    printJoystickSerialIfDue(rawX, rawY, currentTime);

    // Send movement update if changed
    if (movementChanged && isConnected) {
        sendMovement();
    }

    // Send fire command on button press
    if (firePressed && isConnected) {
        sendFire();
    }

    // Update previous states
    prevBtnUp = btnUp;
    prevBtnDown = btnDown;
    prevBtnLeft = btnLeft;
    prevBtnRight = btnRight;
    prevBtnFire = btnFire;
}

void sendMovement() {
    StaticJsonDocument<128> doc;
    doc["type"] = "move";

    JsonObject moving = doc.createNestedObject("moving");
    moving["up"] = btnUp;
    moving["down"] = btnDown;
    moving["left"] = btnLeft;
    moving["right"] = btnRight;

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);

    Serial.print("[SEND] Move: ");
    if (btnUp) Serial.print("UP ");
    if (btnDown) Serial.print("DOWN ");
    if (btnLeft) Serial.print("LEFT ");
    if (btnRight) Serial.print("RIGHT ");
    Serial.println();
}

void sendFire() {
    StaticJsonDocument<64> doc;
    doc["type"] = "fire";

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);

    Serial.println("[SEND] FIRE!");
}

// ============================================
// UTILITY FUNCTIONS
// ============================================

// Auto-reconnect to WiFi if disconnected
void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Connection lost, reconnecting...");
        connectWiFi();
        connectWebSocket();
    }
}
