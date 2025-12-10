#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <DHT.h>
#include "HX711.h"

class SensorManager {
private:
    uint8_t _dhtPin;
    uint8_t _ldrPin;
    uint8_t _dtPin;
    uint8_t _sckPin;
    DHT* _dht;
    HX711* _scale;

public:
    // Constructor
    SensorManager(uint8_t dhtPin, uint8_t ldrPin, uint8_t dtPin, uint8_t sckPin);
    
    // Methods
    void begin();
    float readTemperature();
    float readLightIntensity();
    float readWeight();
};

#endif