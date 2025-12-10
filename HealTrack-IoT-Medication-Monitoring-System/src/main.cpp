#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "Sensors.h"    // Include our new header
#include "Actuators.h"  // Include our new header

// --- Configuration Struct (Holds all adjustable settings) ---
struct SystemConfig {
    int samplingInterval = 5000;
    int sendingInterval = 120000;
    float minAngle = 30.0;
    float controlFactor = 0.75;
    float idealTemp = 30.0;
} sysConfig;

// --- Object Instantiation ---
// Create objects using the classes we defined
SensorManager sensors(12, 33, 13, 14); // DHT_PIN, LDR_PIN, DT_PIN, SCK_PIN
ServoController servoCtrl(27);         // SERVO_PIN

// --- Network & Globals ---
WiFiClient espClient;
PubSubClient client(espClient);
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.emqx.io";

// Timing variables
unsigned long lastSamplingTime = 0;
unsigned long lastSendingTime = 0;
float lightReadings[24];
int readingIndex = 0;
int totalReadings = 0;

// --- Function Declarations for Network (Keep these in main for simplicity) ---
void setupWiFi();
void reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);

void setup() {
    Serial.begin(115200);
    
    // Initialize our Objects
    sensors.begin();
    servoCtrl.begin();
    servoCtrl.setAngle(sysConfig.minAngle);

    setupWiFi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    
    Serial.println("System Initialized with OOP Structure");
}

void loop() {
    if (!client.connected()) reconnectMQTT();
    client.loop();

    unsigned long currentMillis = millis();

    // --- Sampling Logic ---
    if (currentMillis - lastSamplingTime >= sysConfig.samplingInterval) {
        lastSamplingTime = currentMillis;

        // 1. Get Data from Objects
        float temp = sensors.readTemperature();
        float light = sensors.readLightIntensity();
        float weight = sensors.readWeight();

        // 2. Process Servo Logic via Object
        float angle = servoCtrl.calculateAngle(
            light, temp, 
            sysConfig.minAngle, sysConfig.controlFactor, sysConfig.idealTemp, 
            sysConfig.samplingInterval, sysConfig.sendingInterval
        );
        servoCtrl.setAngle(angle);

        // 3. Store Readings for Average
        lightReadings[readingIndex] = light;
        readingIndex = (readingIndex + 1) % (sysConfig.sendingInterval / sysConfig.samplingInterval);
        if (totalReadings < (sysConfig.sendingInterval / sysConfig.samplingInterval)) totalReadings++;

        // 4. Publish (Logic remains in main as it's the "Controller")
        client.publish("medibox/recent_temperature", String(temp, 2).c_str());
        client.publish("medibox/recent_light_intensity", String(light, 2).c_str());
        client.publish("medibox/recent_weight", String(weight, 2).c_str());
        
        Serial.printf("T:%.2f | L:%.2f | W:%.2f | Angle:%.2f\n", temp, light, weight, angle);
    }
}

// ... [Include your setupWiFi, reconnectMQTT, and callback functions here] ...
// Important: In callback(), update the 'sysConfig' struct members!
// Example: if (strcmp(topic, "medibox/minimum_angle") == 0) sysConfig.minAngle = receivedValue;