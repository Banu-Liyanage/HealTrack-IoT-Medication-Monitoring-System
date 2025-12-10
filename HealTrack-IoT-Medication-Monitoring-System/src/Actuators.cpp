#include "Actuators.h"

ServoController::ServoController(uint8_t pin) {
    _servoPin = pin;
}

void ServoController::begin() {
    _servo.attach(_servoPin);
}

void ServoController::setAngle(float angle) {
    _servo.write((int)angle);
}

float ServoController::calculateAngle(float light, float temp, float minAngle, float controlFactor, float idealTemp, int sampling, int sending) {
    float ts = sampling / 1000.0;
    float tu = sending / 1000.0;
    
    float logRatio = log(ts / tu);
    float tempRatio = temp / idealTemp;
    
    float angle = minAngle + ((180.0 - minAngle) * light * controlFactor * logRatio * tempRatio);
    return constrain(angle, 0.0, 180.0);
}