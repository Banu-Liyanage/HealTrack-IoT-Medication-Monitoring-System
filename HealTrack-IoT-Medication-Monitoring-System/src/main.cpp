#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "Sensors.h"
#include "Actuators.h"
#include "Buzzer.h"
#include "Display.h"
#include "time.h"

// --- PIN DEFINITIONS ---
#define DHT_PIN 12
#define LDR_PIN 33          // Pin 33 is ADC1 (Safe for WiFi)
#define LOAD_CELL_DT_PIN 13
#define LOAD_CELL_SCK_PIN 14
#define SERVO_PIN 27
#define BUZZER_PIN 18

// Menu & UI Pins
#define BTN_UP 35
#define BTN_DOWN 32
#define BTN_CANCEL 34
#define BTN_OK 25           // Digital Pin (Safe)
#define LED_ALARM 15
#define LED_WARN 2

// LDR Constants
const float GAMMA = 0.7;
const float RL10 = 50;

// --- OBJECTS ---
SensorManager sensors(DHT_PIN, LDR_PIN, LOAD_CELL_DT_PIN, LOAD_CELL_SCK_PIN);
ServoController servoCtrl(SERVO_PIN);
Buzzer buzzer(BUZZER_PIN);
Display display;

WiFiClient espClient;
PubSubClient client(espClient);

// --- CONFIGURATION ---
struct SystemConfig {
    int samplingInterval = 5000;
    int sendingInterval = 120000;
    float minAngle = 30.0;
    float controlFactor = 0.75;
    float idealTemp = 28.0;
    long utcOffset = 19800; // +5:30
} sysConfig;

// --- GLOBAL VARIABLES ---
unsigned long lastSamplingTime = 0;
bool medicineTakenToday = false;

// Alarms
struct Alarm {
    int hour;
    int minute;
    bool enabled;
    bool triggered;
};
Alarm alarms[2] = {{8, 0, true, false}, {20, 0, false, false}}; 

// Menu Logic
String menuOptions[] = {"Set Timezone", "Set Alarm 1", "Set Alarm 2", "Exit"};
int currentMode = 0;
int maxModes = 4;

// --- FUNCTION DECLARATIONS ---
void setupWiFi();
void reconnectMQTT();
void checkMedicationRoutine();
void runMenu();
int  waitForButton();
void setTimezoneMenu();
void setAlarmMenu(int alarmIndex);
void callback(char* topic, byte* payload, unsigned int length);
void readLDRDebug(); // Renamed for clarity

void setup() {
    Serial.begin(115200);

    // Init Hardware
    pinMode(BTN_UP, INPUT);
    pinMode(BTN_DOWN, INPUT);
    pinMode(BTN_CANCEL, INPUT);
    pinMode(BTN_OK, INPUT);
    pinMode(LED_ALARM, OUTPUT);
    pinMode(LED_WARN, OUTPUT);
    pinMode(LDR_PIN, INPUT);

    sensors.begin();
    servoCtrl.begin();
    buzzer.begin();
    display.begin();
    display.showStartup();
    servoCtrl.setAngle(sysConfig.minAngle);

    // Init Network
    setupWiFi();
    client.setServer("broker.emqx.io", 1883);
    client.setCallback(callback); // IMPORTANT: Register the callback
    
    // Init Time
    configTime(sysConfig.utcOffset, 0, "pool.ntp.org");
    
    Serial.println("System Initialized");
}

void loop() {
    // 1. Connectivity
    if (!client.connected()) reconnectMQTT();
    client.loop();

    // 2. Button Check (Enter Menu?)
    if (digitalRead(BTN_OK) == LOW) {
        delay(200); 
        runMenu();  
    }

    // 3. Alarm Logic
    checkMedicationRoutine();

    // 4. Sampling & Dashboard
    unsigned long currentMillis = millis();
    if (currentMillis - lastSamplingTime >= sysConfig.samplingInterval) {
        lastSamplingTime = currentMillis;

        // Debug LDR (Print to Serial)
        readLDRDebug();

        // Read Sensors (Main Logic)
        float temp = sensors.readTemperature();
        float hum = sensors.readHumidity();
        float light = sensors.readLightIntensity();
        float weight = sensors.readWeight();

        // Warning LED
        if (temp > 32 || temp < 24 || hum > 80) digitalWrite(LED_WARN, HIGH);
        else digitalWrite(LED_WARN, LOW);

        // Update Servo
        float angle = servoCtrl.calculateAngle(light, temp, sysConfig.minAngle, sysConfig.controlFactor, sysConfig.idealTemp, 5000, 120000);
        servoCtrl.setAngle(angle);

        // Update Display
        display.updateDashboard(temp, hum, weight, light, "IDLE");

        // Send MQTT
        client.publish("HealTrack/temp", String(temp, 1).c_str());
        client.publish("HealTrack/weight", String(weight, 1).c_str());
        client.publish("HealTrack/humidity", String(hum, 1).c_str());
    }
}

// ==========================================
//          ALARM & MEDICATION LOGIC
// ==========================================
void checkMedicationRoutine() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    for(int i=0; i<2; i++) {
        if(alarms[i].enabled && !alarms[i].triggered && 
           timeinfo.tm_hour == alarms[i].hour && timeinfo.tm_min == alarms[i].minute && timeinfo.tm_sec == 0) {
            
            alarms[i].triggered = true; // Trigger only once per minute
            Serial.println("ALARM TRIGGERED!");
            client.publish("HealTrack/status", "ALARM");
            
            digitalWrite(LED_ALARM, HIGH);
            display.showAlarmScreen();
            
            float weightBefore = sensors.readWeight();
            servoCtrl.setAngle(90);
            
            bool actionTaken = false;
            unsigned long alarmStart = millis();

            while(!actionTaken) {
                buzzer.ring(); delay(200);
                buzzer.stop(); delay(200);
                client.loop();

                // SNOOZE
                if(digitalRead(BTN_OK) == LOW) {
                    Serial.println("SNOOZED");
                    alarms[i].minute += 5;
                    if(alarms[i].minute >= 60) {
                         alarms[i].minute %= 60; 
                         alarms[i].hour = (alarms[i].hour + 1) % 24;
                    }
                    alarms[i].triggered = false; 
                    actionTaken = true;
                }

                // TIMEOUT (30s)
                if(millis() - alarmStart > 30000) actionTaken = true;
            }

            digitalWrite(LED_ALARM, LOW);
            servoCtrl.setAngle(sysConfig.minAngle);
            float weightAfter = sensors.readWeight();

            if(weightBefore - weightAfter > 2.0) {
                client.publish("HealTrack/med_status", "TAKEN");
                Serial.println("Meds Taken");
            } else {
                client.publish("HealTrack/med_status", "MISSED");
                Serial.println("Meds Missed");
            }
        }
    }
}

// ==========================================
//              MENU SYSTEM
// ==========================================
void runMenu() {
    bool inMenu = true;
    while(inMenu) {
        display.drawMenu(menuOptions, currentMode, maxModes);
        int pressed = waitForButton();

        if (pressed == BTN_DOWN) {
            currentMode = (currentMode + 1) % maxModes;
        } 
        else if (pressed == BTN_UP) {
            currentMode--;
            if (currentMode < 0) currentMode = maxModes - 1;
        } 
        else if (pressed == BTN_OK) {
            if (currentMode == 0) setTimezoneMenu();
            else if (currentMode == 1) setAlarmMenu(0);
            else if (currentMode == 2) setAlarmMenu(1);
            else if (currentMode == 3) inMenu = false; 
        }
        else if (pressed == BTN_CANCEL) inMenu = false;
    }
    display.clear(); 
}

int waitForButton() {
    while(true) {
        client.loop(); 
        if (digitalRead(BTN_UP) == LOW) { delay(200); return BTN_UP; }
        if (digitalRead(BTN_DOWN) == LOW) { delay(200); return BTN_DOWN; }
        if (digitalRead(BTN_OK) == LOW) { delay(200); return BTN_OK; }
        if (digitalRead(BTN_CANCEL) == LOW) { delay(200); return BTN_CANCEL; }
    }
}

void setTimezoneMenu() {
    int hours = sysConfig.utcOffset / 3600;
    while(true) {
        display.drawTimeSet(hours, 0, "Set UTC Offset");
        int btn = waitForButton();
        if(btn == BTN_UP) hours++;
        if(btn == BTN_DOWN) hours--;
        if(btn == BTN_OK) {
            sysConfig.utcOffset = hours * 3600;
            configTime(sysConfig.utcOffset, 0, "pool.ntp.org");
            break;
        }
        if(btn == BTN_CANCEL) break;
    }
}

void setAlarmMenu(int index) {
    int h = alarms[index].hour;
    int m = alarms[index].minute;
    bool settingHour = true;

    while(true) {
        if(settingHour) display.drawTimeSet(h, m, "Set Alarm Hour");
        else display.drawTimeSet(h, m, "Set Alarm Min");

        int btn = waitForButton();
        if(btn == BTN_UP) {
            if(settingHour) h = (h + 1) % 24;
            else m = (m + 1) % 60;
        }
        if(btn == BTN_DOWN) {
            if(settingHour) h = (h - 1 < 0) ? 23 : h - 1;
            else m = (m - 1 < 0) ? 59 : m - 1;
        }
        if(btn == BTN_OK) {
            if(settingHour) settingHour = false;
            else {
                alarms[index].hour = h;
                alarms[index].minute = m;
                alarms[index].enabled = true;
                alarms[index].triggered = false;
                break;
            }
        }
        if(btn == BTN_CANCEL) break;
    }
}

// --- NETWORK & DEBUG ---
void setupWiFi() {
    WiFi.begin("Wokwi-GUEST", "");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi Connected");
}

void reconnectMQTT() {
    while (!client.connected()) {
        if (client.connect("HealTrack_ESP32_Final")) {
            Serial.println("MQTT Connected");
            client.subscribe("HealTrack/inputs/set_alarm"); // Listen for dashboard commands
        } else {
            delay(5000);
        }
    }
}

// Callback for Dashboard Control
void callback(char* topic, byte* payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    String msgString = String(message);

    Serial.printf("Received [%s]: %s\n", topic, message);

    if (String(topic) == "HealTrack/inputs/set_alarm") {
        int separatorIndex = msgString.indexOf(':');
        if (separatorIndex != -1) {
            String hourStr = msgString.substring(0, separatorIndex);
            String minStr = msgString.substring(separatorIndex + 1);

            alarms[0].hour = hourStr.toInt();
            alarms[0].minute = minStr.toInt();
            alarms[0].enabled = true;
            alarms[0].triggered = false;

            Serial.printf("Remote: Alarm set to %d:%d\n", alarms[0].hour, alarms[0].minute);
            client.publish("HealTrack/status", "ALARM UPDATED");
        }
    }
}

// Corrected LDR Debug Function
void readLDRDebug() {
    int analogValue = analogRead(LDR_PIN);
    
    // 1. Calculate Voltage
    float voltageRead = analogValue / 4095.0 * 3.3;
    
    // 2. Avoid Division by Zero
    
    if (voltageRead >= 3.29) {
        Serial.println("LDR: Too Bright / Max Voltage");
        return;
    }
    
    // 3. Calculate Resistance using READ voltage, NOT global constant
    float resistance = 2000 * voltageRead / (1 - voltageRead / 3.3);
    
    // 4. Calculate Lux
    float lux = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1 / GAMMA));
    
    Serial.print("LDR Intensity: ");
    Serial.print(lux);
    Serial.println(" lux");
}