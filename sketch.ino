#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// Pin Definitions
#define LDR_PIN 33        // Analog pin for LDR
#define DHT_PIN 12        // Digital pin for DHT11
#define SERVO_PIN 27      // PWM pin for servo motor
#define DHT_TYPE DHT22    // DHT sensor type

// WiFi credentials
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT Broker settings
// const char* mqtt_server = "test.mosquitto.org";
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_client_id = "medibox_esp32_wokwi_unique";

// MQTT Topics
const char* topic_light_intensity = "medibox/recent_light_intensity";
const char* topic_temperature = "medibox/recent_temperature";
const char* topic_sampling_interval = "medibox/sampling_interval";
const char* topic_sending_interval = "medibox/sending_interval";
const char* topic_min_angle = "medibox/minimum_angle";
const char* topic_control_factor = "medibox/control_factor";
const char* topic_ideal_temp = "medibox/storage_temperature";

// Global variables
WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHT_PIN, DHT_TYPE);
Servo shadeServo;

// Default parameters (configurable via MQTT)
int samplingInterval = 5000;         // 5 seconds in milliseconds
int sendingInterval = 120000;        // 2 minutes in milliseconds
float minAngle = 30.0;               // Default minimum angle (θoffset)
float controlFactor = 0.75;          // Default controlling factor (γ)
float idealTemp = 30.0;              // Default ideal medicine storage temperature (Tmed)

// Variables for light intensity measurements
unsigned long lastSamplingTime = 0;
unsigned long lastSendingTime = 0;
float lightReadings[24];             // Store up to 24 readings (2 minutes with 5-second intervals)
int readingIndex = 0;
int totalReadings = 0;

// Function declarations
void setupWiFi();
void reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);
float readLightIntensity();
float readTemperature();
float calculateServoAngle(float lightIntensity, float temperature);
void updateServoPosition(float angle);
float calculateAverageLightIntensity();

void setup() {
  Serial.begin(115200);
  
  // Initialize pin modes
  pinMode(LDR_PIN, INPUT);
  
  // Initialize DHT sensor
  dht.begin();
  
  // Initialize servo
  shadeServo.attach(SERVO_PIN);
  shadeServo.write(minAngle);  // Set to minimum angle initially
  
  // Setup WiFi connection
  setupWiFi();
  
  // Setup MQTT connection
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  Serial.println("Medibox system initialized");
}

void loop() {
  // Ensure MQTT connection is maintained
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // Check if it's time to sample light intensity
  unsigned long currentMillis = millis();
  if (currentMillis - lastSamplingTime >= samplingInterval) {
    lastSamplingTime = currentMillis;
    
    // Read and store light intensity
   
    float lightIntensity = readLightIntensity();
    lightReadings[readingIndex] = lightIntensity;
    readingIndex = (readingIndex + 1) % (sendingInterval / samplingInterval);
    if (totalReadings < (sendingInterval / samplingInterval)) {
      totalReadings++;
    }
    
    Serial.print("Light Intensity Sample: ");
    Serial.println(lightIntensity);
    
    // Read temperature
    float temperature = readTemperature();
    
    // Calculate servo angle based on current conditions
    float servoAngle = calculateServoAngle(lightIntensity, temperature);
    
    // Update servo position
    updateServoPosition(servoAngle);
    
    // Publish temperature and servo angle immediately
    char tempStr[8];
    dtostrf(temperature, 1, 2, tempStr);
    client.publish(topic_temperature, tempStr);
    
    char lightStr[8];
     dtostrf(lightIntensity, 1, 2, lightStr);
    client.publish(topic_light_intensity, lightStr);
  }
  
  // Check if it's time to send the average light intensity
  if (currentMillis - lastSendingTime >= sendingInterval && totalReadings > 0) {
    lastSendingTime = currentMillis;
    
    // Calculate and send average light intensity
    float avgIntensity = calculateAverageLightIntensity();
    
    char avgStr[8];
    dtostrf(avgIntensity, 1, 4, avgStr);
    client.publish(topic_light_intensity, avgStr);
    
    Serial.print("Average Light Intensity Sent: ");
    Serial.println(avgIntensity);
  }
}

void setupWiFi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_client_id)) {
      Serial.println("connected");
      
      // Subscribe to configuration topics
      client.subscribe(topic_sampling_interval);
      client.subscribe(topic_sending_interval);
      client.subscribe(topic_min_angle);
      client.subscribe(topic_control_factor);
      client.subscribe(topic_ideal_temp);
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Convert payload to a string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(message);
  
  // Parse the received value
  float receivedValue = atof(message);
  
  // Update the appropriate parameter based on the topic
  if (strcmp(topic, topic_sampling_interval) == 0) {
    // Convert from seconds to milliseconds
    samplingInterval = (int)(receivedValue * 1000);
    Serial.print("Sampling interval updated to: ");
    Serial.println(samplingInterval);
    
    // Reset the readings array since the interval changed
    readingIndex = 0;
    totalReadings = 0;
  }
  else if (strcmp(topic, topic_sending_interval) == 0) {
    // Convert from seconds to milliseconds
    sendingInterval = (int)(receivedValue * 60 * 1000);
    Serial.print("Sending interval updated to: ");
    Serial.println(sendingInterval);
    
    // Reset the readings array since the interval changed
    readingIndex = 0;
    totalReadings = 0;
  }
  else if (strcmp(topic, topic_min_angle) == 0) {
    minAngle = receivedValue;
    Serial.print("Minimum angle updated to: ");
    Serial.println(minAngle);
  }
  else if (strcmp(topic, topic_control_factor) == 0) {
    controlFactor = receivedValue;
    Serial.print("Control factor updated to: ");
    Serial.println(controlFactor);
  }
  else if (strcmp(topic, topic_ideal_temp) == 0) {
    idealTemp = receivedValue;
    Serial.print("Ideal temperature updated to: ");
    Serial.println(idealTemp);
  }
}

float readLightIntensity() {
  // Read the analog value from LDR
  int rawValue = analogRead(LDR_PIN);
  
  // Convert analog reading (0-4063 for ESP32) to a normalized value (0-1)
  float normalizedValue = (float)rawValue/ 4063.0;
  
  return normalizedValue; //normalizedValue;
}

float readTemperature() {
  // Read temperature from DHT11
  float temperature = dht.readTemperature();
  
  // Check if reading is valid
  if (isnan(temperature)) {
    Serial.println("Failed to read temperature from DHT sensor!");
    return 0.0;
  }
  
  return temperature;
}

float calculateServoAngle(float lightIntensity, float temperature) {
  // Calculate servo angle using the provided equation:
  // θ = θoffset + (180 - θoffset) × I × γ × ln(ts/tu) × (T/Tmed)
  
  // Convert milliseconds to seconds for the calculation
  float ts = samplingInterval / 1000.0;
  float tu = sendingInterval / 1000.0;
  
  // Apply the formula
  float logRatio = log(ts / tu);
  float tempRatio = temperature / idealTemp;
  
  float angle = minAngle + ((180.0 - minAngle) * lightIntensity * controlFactor * logRatio * tempRatio);
  
  // Ensure the angle is within valid range
  angle = constrain(angle, 0.0, 180.0);
  
  Serial.print("Calculated servo angle: ");
  Serial.println(angle);
  
  return angle;
}

void updateServoPosition(float angle) {
  // Update servo motor position
  shadeServo.write((int)angle);
  
  Serial.print("Servo position updated to: ");
  Serial.println(angle);
}

float calculateAverageLightIntensity() {
  if (totalReadings == 0) {
    return 0.0;
  }
  
  float sum = 0.0;
  for (int i = 0; i < totalReadings; i++) {
    sum += lightReadings[i];
  }
  
  return sum / totalReadings;
}