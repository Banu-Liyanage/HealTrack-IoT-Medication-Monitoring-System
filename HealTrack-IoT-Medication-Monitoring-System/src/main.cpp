#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "Sensors.h"
#include "Actuators.h"
#include "Buzzer.h"
#include "Display.h"
#include "time.h"

// --- PIN DEFINITIONS ---
// Old Pins
#define DHT_PIN 12
#define LDR_PIN 33
#define LOAD_CELL_DT_PIN 13
#define LOAD_CELL_SCK_PIN 14
#define SERVO_PIN 27
#define BUZZER_PIN 18
// New Pins (Menu & LEDs)
#define BTN_UP 35
#define BTN_DOWN 32
#define BTN_CANCEL 34
#define BTN_OK 25       // Changed from 33 to avoid conflict with LDR
#define LED_ALARM 15
#define LED_WARN 2
const float GAMMA = 0.7;
const float RL10 = 50;
const float voltage = 3.3;
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
    // Timezone
    long utcOffset = 19800; // Default 5.5 hours
} sysConfig;

// --- GLOBAL VARIABLES ---
unsigned long lastSamplingTime = 0;
bool medicineTakenToday = false;

// Alarms (Up to 2 for now, based on menu options)
struct Alarm {
    int hour;
    int minute;
    bool enabled;
    bool triggered;
};
Alarm alarms[2] = {{8, 0, true, false}, {20, 0, false, false}}; // Defaults

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
void LDR();


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
    
    // Init Time
    configTime(sysConfig.utcOffset, 0, "pool.ntp.org");
    
    Serial.println("System Initialized (Merged Mode)");
}

void loop() {
    // 1. Connectivity
    if (!client.connected()) reconnectMQTT();
    client.loop();

    // 2. Button Check (Enter Menu?)
    if (digitalRead(BTN_OK) == LOW) {
        delay(200); // Debounce
        runMenu();  // Enter blocking menu mode
    }

    // 3. Alarm Logic (Merged)
    checkMedicationRoutine();
    LDR();
    // 4. Dashboard & Sensors (Normal Mode)
    unsigned long currentMillis = millis();
    if (currentMillis - lastSamplingTime >= sysConfig.samplingInterval) {
        lastSamplingTime = currentMillis;

        // Read Sensors
        float temp = sensors.readTemperature();
        float hum = sensors.readHumidity();
        float light = sensors.readLightIntensity();
        float weight = sensors.readWeight();

        // Check Warning LED Logic
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
    }
}

// ==========================================
//           ALARM & MEDICATION LOGIC
// ==========================================
void checkMedicationRoutine() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    // Check all enabled alarms
    for(int i=0; i<2; i++) {
        if(alarms[i].enabled && !alarms[i].triggered && 
           timeinfo.tm_hour == alarms[i].hour && timeinfo.tm_min == alarms[i].minute) {
            
            // --- ALARM TRIGGERED ---
            alarms[i].triggered = true;
            Serial.println("ALARM TRIGGERED!");
            client.publish("HealTrack/status", "ALARM");
            
            digitalWrite(LED_ALARM, HIGH);
            display.showAlarmScreen();
            
            // 1. Measure Weight Before
            float weightBefore = sensors.readWeight();

            // 2. Open Box
            servoCtrl.setAngle(90);
            
            // 3. Alarm Loop (Ring until Taken OR Snoozed)
            bool actionTaken = false;
            unsigned long alarmStart = millis();

            while(!actionTaken) {
                buzzer.ring();
                delay(200);
                buzzer.stop();
                delay(200);

                // Keep MQTT alive
                client.loop();

                // CHECK SNOOZE (OK Button)
                if(digitalRead(BTN_OK) == LOW) {
                    Serial.println("SNOOZED");
                    alarms[i].minute += 5; // Add 5 mins
                    if(alarms[i].minute >= 60) {
                         alarms[i].minute %= 60; 
                         alarms[i].hour = (alarms[i].hour + 1) % 24;
                    }
                    alarms[i].triggered = false; // Reset trigger so it rings again
                    actionTaken = true;
                }

                // TIMEOUT - Simulate patient taking meds
                if(millis() - alarmStart > 30000) {
                     actionTaken = true; // Stop ringing eventually
                }
            }

            // 4. Close Box & Check Weight
            digitalWrite(LED_ALARM, LOW);
            //servoCtrl.setAngle(sysConfig.minAngle);
            float weightAfter = sensors.readWeight();
            //LDR();
            // 5. Determine Taken or Missed
            if(weightBefore - weightAfter > 2.0) {
                client.publish("HealTrack/med_status", "TAKEN");
                Serial.println("Meds Taken");
            } else {
                client.publish("HealTrack/med_status", "MISSED");
                Serial.println("Meds Missed");
            }
        }
    }

    // Reset alarms at midnight
    if(timeinfo.tm_hour == 0 && timeinfo.tm_min == 0) {
        for(int i=0; i<2; i++) alarms[i].triggered = false;
    }
}

// ==========================================
//           MENU SYSTEM
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
            else if (currentMode == 3) inMenu = false; // Exit
        }
        else if (pressed == BTN_CANCEL) {
            inMenu = false;
        }
    }
    display.clear(); // Clear before returning to dashboard
}

int waitForButton() {
    while(true) {
        client.loop(); // Keep MQTT alive while waiting!
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
            if(settingHour) settingHour = false; // Move to minute
            else {
                // Save
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

// --- NETWORK HELPERS ---
void setupWiFi() {
    WiFi.begin("Wokwi-GUEST", "");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("WiFi Connected");
}

void reconnectMQTT() {
    while (!client.connected()) {
        if (client.connect("HealTrack_Client_Combined")) {
            Serial.println("MQTT Connected");
        } else {
            delay(5000);
        }
    }
}
// --- Callback for Remote Control ---
void callback(char* topic, byte* payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    String msgString = String(message);

    Serial.printf("Received [%s]: %s\n", topic, message);

    // Check if the message is for setting the alarm
    if (String(topic) == "HealTrack/inputs/set_alarm") {
        // Expected format "HH:MM" e.g., "14:30"
        int separatorIndex = msgString.indexOf(':');
        if (separatorIndex != -1) {
            String hourStr = msgString.substring(0, separatorIndex);
            String minStr = msgString.substring(separatorIndex + 1);

            int newH = hourStr.toInt();
            int newM = minStr.toInt();

            
            alarms[0].hour = newH;
            alarms[0].minute = newM;
            alarms[0].enabled = true;
            alarms[0].triggered = false;

            Serial.printf("Remote: Alarm set to %d:%02d\n", newH, newM);
            
            // Confirm back to Dashboard
            client.publish("HealTrack/status", "ALARM UPDATED");
        }
    }
}
void LDR(){
    analogRead(LDR_PIN);
    float LDR_voltage = analogRead(LDR_PIN) * (3.3 / 4095.0);
    float resistance = 2000 * voltage / (1 - voltage / 5);
    float lux = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1 / GAMMA));
    Serial.print("LDR Light Intensity: ");
    Serial.print(lux);
    Serial.println(" lux");

  }