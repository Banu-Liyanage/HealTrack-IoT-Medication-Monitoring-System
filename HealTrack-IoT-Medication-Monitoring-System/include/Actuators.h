#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <Arduino.h>
#include <ESP32Servo.h>

class ServoController {
private:
    uint8_t _servoPin;
    Servo _servo;

public:
    ServoController(uint8_t pin);
    void begin();
    void setAngle(float angle);
    
    // The complex calculation logic is moved here
    float calculateAngle(float light, float temp, float minAngle, float controlFactor, float idealTemp, int sampling, int sending);
};

#endif