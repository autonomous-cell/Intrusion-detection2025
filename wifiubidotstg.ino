#include <WiFi.h>
#include <HX711.h>
#include <HTTPClient.h>

// ========== TOKENS ==========
#define UBIDOTS_TOKEN "BBUS-jIRPIUV0F8JQmt7Ow8jqcdY8SQ7B6o"
#define TELEGRAM_BOT "8643824216:AAF1UjqCSPIAwmiXMnZViy9arAKa2M3I1gQ"
#define CHAT_ID "5284599852"

// ========== PINS ==========
#define LOAD_DT         4
#define LOAD_SCK        5
#define VIBRATION_PIN   6
#define TRIG_PIN        8
#define ECHO_PIN        9
#define DOOR_PIN        10
#define WINDOW_PIN      11
#define SOUND_DO        12
#define SOUND_AO        1
#define PIR_PIN         13
#define BUZZER_PIN      14

// ========== THRESHOLDS ==========
#define LOAD_THRESHOLD      20.0
#define ULTRA_THRESHOLD     50.0
#define SOUND_AO_THRESHOLD  2000
#define VIBRATION_THRESHOLD 1

#define PRIMARY_MIN         1
#define SECONDARY_MIN       2
#define TIME_WINDOW         5000

HX711 scale;
float calFactor = 0.02;

char ssid[32] = "";
char pass[64] = "";

bool alarmArmed = true;
bool intrusionDetected = false;
unsigned long firstTrigger = 0;

void getSerialInput(const char* prompt, char* buffer, int len) {
    Serial.print(prompt);
    int i = 0;
    while (i < len - 1) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n') break;
            buffer[i++] = c;
        }
    }
    buffer[i] = '\0';
    Serial.println(buffer);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
    delay(1000);
    
    Serial.println("\n========================================");
    Serial.println("   IDS SYSTEM - SERIAL CONFIGURATION");
    Serial.println("========================================\n");
    
    getSerialInput("Enter WiFi SSID: ", ssid, 32);
    getSerialInput("Enter WiFi Password: ", pass, 64);
    
    Serial.println("\n--- Configuring Hardware ---");
    
    pinMode(VIBRATION_PIN, INPUT);
    pinMode(DOOR_PIN, INPUT_PULLUP);
    pinMode(WINDOW_PIN, INPUT_PULLUP);
    pinMode(SOUND_DO, INPUT);
    pinMode(PIR_PIN, INPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    
    scale.begin(LOAD_DT, LOAD_SCK);
    scale.set_gain(128);
    scale.tare(20);
    scale.set_scale(calFactor);
    Serial.println("Load sensor ready");
    
    Serial.println("\n--- Connecting to WiFi ---");
    WiFi.begin(ssid, pass);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi CONNECTED");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi FAILED");
        while (1) delay(1000);
    }
    
    Serial.println("\n========================================");
    Serial.println("   IDS SYSTEM READY");
    Serial.println("========================================\n");
}

float getLoadWeight() {
    if (!scale.is_ready()) return 0;
    float weight = scale.get_units(10);
    if (weight < 0.0f) weight = 0.0f;
    if (weight > 20) {
        Serial.print("[LOAD] EXCEEDS MAX: ");
        Serial.println(weight);
        return 0;
    }
    return weight;
}

float getUltrasonic() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    if (duration == 0) return 999;
    return duration * 0.034 / 2;
}

void sendToUbidots(bool door, bool window, float load, bool pir, bool vibration, int sound, float ultrasonic, int primary, int secondary, bool alarm) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    String url = "http://industrial.api.ubidots.com/api/v1.6/devices/ids-esp32";
    http.begin(url);
    http.addHeader("X-Auth-Token", UBIDOTS_TOKEN);
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{";
    payload += "\"door\":" + String(door ? 1 : 0) + ",";
    payload += "\"window\":" + String(window ? 1 : 0) + ",";
    payload += "\"load\":" + String(load, 2) + ",";
    payload += "\"pir\":" + String(pir ? 1 : 0) + ",";
    payload += "\"vibration\":" + String(vibration ? 1 : 0) + ",";
    payload += "\"sound\":" + String(sound) + ",";
    payload += "\"ultrasonic\":" + String(ultrasonic, 1) + ",";
    payload += "\"primary\":" + String(primary) + ",";
    payload += "\"secondary\":" + String(secondary) + ",";
    payload += "\"alarm\":" + String(alarm ? 1 : 0);
    payload += "}";
    
    int code = http.POST(payload);
    Serial.print("[UBIDOTS] ");
    Serial.println(code == 200 ? "OK" : "FAIL " + String(code));
    http.end();
}

void sendTelegramAlert(String message) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT) + "/sendMessage";
    url += "?chat_id=" + String(CHAT_ID) + "&text=" + message;
    http.begin(url);
    int code = http.GET();
    Serial.print("[TELEGRAM] ");
    Serial.println(code == 200 ? "OK" : "FAIL " + String(code));
    http.end();
}

void scanSensors() {
    Serial.println("\n--- SCANNING ---");
    
    bool door = !digitalRead(DOOR_PIN);
    bool window = !digitalRead(WINDOW_PIN);
    float load = getLoadWeight();
    bool loadTrigger = load > LOAD_THRESHOLD;
    
    Serial.print("[P] Door:"); Serial.print(door);
    Serial.print(" Win:"); Serial.print(window);
    Serial.print(" Load:"); Serial.print(load, 2);
    Serial.print("kg T:"); Serial.println(loadTrigger);
    
    int primary = door + window + loadTrigger;
    
    bool pir = digitalRead(PIR_PIN);
    bool vibration = digitalRead(VIBRATION_PIN) == VIBRATION_THRESHOLD;
    int soundValue = analogRead(SOUND_AO);
    bool soundTrigger = soundValue > SOUND_AO_THRESHOLD;
    float distance = getUltrasonic();
    bool ultraTrigger = distance < ULTRA_THRESHOLD;
    
    Serial.print("[S] PIR:"); Serial.print(pir);
    Serial.print(" Vib:"); Serial.print(vibration);
    Serial.print(" Snd:"); Serial.print(soundValue);
    Serial.print(" Ult:"); Serial.print(distance, 1);
    Serial.print("cm T:"); Serial.println(ultraTrigger);
    
    int secondary = pir + vibration + soundTrigger + ultraTrigger;
    
    Serial.print("Counts P:"); Serial.print(primary);
    Serial.print(" S:"); Serial.println(secondary);
    
    sendToUbidots(door, window, load, pir, vibration, soundValue, distance, primary, secondary, intrusionDetected);
    
    if (primary > 0 || secondary > 0) {
        if (firstTrigger == 0) {
            firstTrigger = millis();
            Serial.println("[TIMER] Start");
        }
        unsigned long elapsed = millis() - firstTrigger;
        if (elapsed <= TIME_WINDOW) {
            if (primary >= PRIMARY_MIN && secondary >= SECONDARY_MIN) {
                Serial.println("[ALERT] INTRUSION");
                intrusionDetected = true;
                triggerAlarm(door, window, loadTrigger, pir, vibration, soundTrigger, ultraTrigger);
            } else {
                Serial.print("[WAIT] T-"); Serial.println(TIME_WINDOW - elapsed);
            }
        } else {
            Serial.println("[TIMEOUT] Reset");
            firstTrigger = 0;
        }
    } else {
        if (firstTrigger != 0) Serial.println("[CLEAR] Reset");
        firstTrigger = 0;
    }
}

void triggerAlarm(bool d, bool w, bool l, bool p, bool v, bool s, bool u) {
    digitalWrite(BUZZER_PIN, HIGH);
    
    String reason = "🔴 INTRUSION: ";
    if (d) reason += "Door ";
    if (w) reason += "Window ";
    if (l) reason += "Load ";
    reason += "+ ";
    if (p) reason += "PIR ";
    if (v) reason += "Vib ";
    if (s) reason += "Sound ";
    if (u) reason += "Ultra ";
    
    Serial.println("\n*** ALARM ***");
    Serial.println(reason);
    
    sendTelegramAlert(reason);
    sendToUbidots(d, w, 0, p, v, 0, 0, 1, 2, true);
    
    Serial.println("[UBIDOTS] Alarm sent");
    Serial.println("[TELEGRAM] Alert sent");
}

void loop() {
    if (alarmArmed && !intrusionDetected) {
        scanSensors();
    } else if (intrusionDetected) {
        digitalWrite(BUZZER_PIN, HIGH);
        Serial.println("[ALARM] ACTIVE");
        delay(1000);
    } else {
        Serial.println("[DISARMED]");
        delay(1000);
    }
}