#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "Sensors.h"    
#include "Actuators.h"  
#include "Buzzer.h"     
#include "time.h"

// --- PIN DEFINITIONS ---
#define DHT_PIN 12
#define LDR_PIN 33
#define LOAD_CELL_DT_PIN 13
#define LOAD_CELL_SCK_PIN 14
#define SERVO_PIN 27
#define BUZZER_PIN 18

// --- CONSTANTS ---
#define MAX_READINGS 24 // Safety limit for the array

struct SystemConfig {               
    int samplingInterval = 5000;   // 5 seconds
    int sendingInterval = 120000;  // 2 minutes
    float minAngle = 30.0;
    float controlFactor = 0.75;
    float idealTemp = 28.0;
} sysConfig;

// Object Instantiation
// FIXED: Corrected LOAD_CELL_DT_PIN typo
SensorManager sensors(DHT_PIN, LDR_PIN, LOAD_CELL_DT_PIN, LOAD_CELL_SCK_PIN);
ServoController servoCtrl(SERVO_PIN);
Buzzer buzzer(BUZZER_PIN);       

WiFiClient espClient;
PubSubClient client(espClient);

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.emqx.io";
const char* mqtt_client_id = "HeelTrackClient";

// Time Settings (NTP)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800; // GMT +5:30 
const int   daylightOffset_sec = 0;

unsigned long lastSamplingTime = 0;
unsigned long lastSendingTime = 0;

// FIXED: Added MAX_READINGS to prevent crashes
float lightReadings[MAX_READINGS];
int readingIndex = 0;
int totalReadings = 0;

// Medication Tracking Variables
int alarmHour = 8;     
int alarmMinute = 0;
bool medicineTakenToday = false;
bool alarmTriggeredThisMinute = false; // logic flag to prevent spamming

void setupWiFi();
void reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);
float calculateAverageLightIntensity();
void checkMedicationRoutine();

void setup() {
    Serial.begin(115200);

    sensors.begin();
    servoCtrl.begin();
    buzzer.begin();
    servoCtrl.setAngle(sysConfig.minAngle);

    setupWiFi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    Serial.println("System Initialized: Algorithm, Sensors, Actuators are set up.");
}

void loop() {
    if (!client.connected()) reconnectMQTT();
    client.loop();

    unsigned long currentMillis = millis();

    checkMedicationRoutine(); 
    
    if (currentMillis - lastSamplingTime >= sysConfig.samplingInterval) {
        lastSamplingTime = currentMillis;
        
        float temp = sensors.readTemperature();
        float humidity = sensors.readHumidity();
        float light = sensors.readLightIntensity();
        float weight = sensors.readWeight();
        
        float angle = servoCtrl.calculateAngle(
            light, temp, 
            sysConfig.minAngle, sysConfig.controlFactor, sysConfig.idealTemp, 
            sysConfig.samplingInterval, sysConfig.sendingInterval
        );
        servoCtrl.setAngle(angle);

        // FIXED: Prevent array overflow
        if (readingIndex < MAX_READINGS) {
            lightReadings[readingIndex] = light;
            readingIndex++; 
            if (totalReadings < MAX_READINGS) totalReadings++;
        }
        
        // Reset index if we hit the calculated limit or the hard array limit
        int maxIndexCalc = sysConfig.sendingInterval / sysConfig.samplingInterval;
        if (readingIndex >= maxIndexCalc || readingIndex >= MAX_READINGS) {
            readingIndex = 0;
        }

        client.publish("HealTrack/recent_temperature", String(temp, 2).c_str());
        client.publish("HealTrack/recent_humidity", String(humidity, 2).c_str());
        client.publish("HealTrack/recent_light_intensity", String(light, 2).c_str());
        client.publish("HealTrack/recent_weight", String(weight, 2).c_str());
        
        Serial.printf("T:%.2f | L:%.2f | W:%.2f | Angle:%.2f\n", temp, humidity, light, weight, angle);
    }

    if (currentMillis - lastSendingTime >= sysConfig.sendingInterval && totalReadings > 0) {
        lastSendingTime = currentMillis;
        float avgIntensity = calculateAverageLightIntensity();
        
        client.publish("medibox/recent_light_intensity", String(avgIntensity, 4).c_str());
        Serial.print("Sending Average Light: "); Serial.println(avgIntensity);
    }
}

void checkMedicationRoutine(){
    struct tm timeinfo;
    // Don't print to Serial on failure here, or it will spam the log 1000s of times
    if (!getLocalTime(&timeinfo)) {
        return;
    }

    // Logic to ensure alarm runs only once per minute
    if (timeinfo.tm_min != alarmMinute) {
        alarmTriggeredThisMinute = false; 
    }

    if(timeinfo.tm_hour == alarmHour && timeinfo.tm_min == alarmMinute && !medicineTakenToday && !alarmTriggeredThisMinute) {
       
        alarmTriggeredThisMinute = true; // Mark as run so we don't repeat immediately
        
        Serial.println("Medication Time! Please take your medicine."); 
        client.publish("HealTrack/medication_alert", " Medication Time! Please take your medicine.");
        
        // FIXED: Record weight BEFORE opening
        float weightBefore = sensors.readWeight(); 

        buzzer.ring();
        servoCtrl.setAngle(90.0); // Open the box (FIXED: servoCtrl typo)
        
        // IMPROVEMENT: Non-blocking delay loop to keep MQTT alive
        for(int i=0; i<100; i++) {
            client.loop(); 
            delay(100); 
        }
        
        buzzer.stop();
        servoCtrl.setAngle(sysConfig.minAngle); // Close the box

        float weightAfter = sensors.readWeight();
        float weightDiff = weightBefore - weightAfter; // Now weightBefore is valid

        Serial.print("Weight Difference : "); Serial.println(weightDiff);

        if(weightDiff > 5.0) { 
            Serial.println("Medicine Taken.");
            client.publish("HealTrack/medication_status", "Medicine Taken.");
            medicineTakenToday = true;
        } else {
            Serial.println("Medicine Not Taken.");
            client.publish("HealTrack/medication_status", "Medicine Not Taken.");
            // Note: medicineTakenToday remains false, so it will try again tomorrow (or you need logic to retry later)
        }

        client.publish("HealTrack/status","IDLE"); // FIXED: HealTrack typo
    }

    // Reset for the next day
    if(timeinfo.tm_hour == 0 && timeinfo.tm_min == 0) {
        medicineTakenToday = false;
    }
}

void setupWiFi(){
    delay(10);
    Serial.println();
    Serial.print("Connecting to "); Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected. IP address: "); Serial.println(WiFi.localIP());
}

void reconnectMQTT(){
    // Loop until we're reconnected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect(mqtt_client_id)) {
            Serial.println("connected");
            client.subscribe("medibox/sampling_interval");
            client.subscribe("medibox/sending_interval");
            client.subscribe("medibox/minimum_angle");
            client.subscribe("medibox/control_factor");
            client.subscribe("medibox/storage_temperature");
        } else {
            Serial.print("failed, rc="); Serial.print(client.state());
            Serial.println(" try again in 5s");
            delay(5000);
        }
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    char message[50]; // FIXED: Use fixed buffer size for safety
    if (length >= 50) length = 49;
    memcpy(message, payload, length);
    message[length] = '\0';
    float val = atof(message);

    Serial.printf("Config Update [%s]: %.2f\n", topic, val);

    if (strcmp(topic, "medibox/sampling_interval") == 0) {
        if(val > 0) { // Protect against divide by zero
            sysConfig.samplingInterval = (int)(val * 1000);
            readingIndex = 0; totalReadings = 0;
        }
    }
    else if (strcmp(topic, "medibox/sending_interval") == 0) {
         if(val > 0) {
            sysConfig.sendingInterval = (int)(val * 60 * 1000);
            readingIndex = 0; totalReadings = 0;
         }
    }
    else if (strcmp(topic, "medibox/minimum_angle") == 0) sysConfig.minAngle = val;
    else if (strcmp(topic, "medibox/control_factor") == 0) sysConfig.controlFactor = val;
    else if (strcmp(topic, "medibox/storage_temperature") == 0) sysConfig.idealTemp = val;
}

float calculateAverageLightIntensity() {
    if (totalReadings == 0) return 0.0;
    float sum = 0.0;
    for (int i = 0; i < totalReadings; i++) sum += lightReadings[i];
    return sum / totalReadings;
}