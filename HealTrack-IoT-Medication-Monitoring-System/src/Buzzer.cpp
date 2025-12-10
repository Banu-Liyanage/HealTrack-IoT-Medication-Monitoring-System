#include "Buzzer.h"

Buzzer::Buzzer(uint8_t pin) { _pin = pin; }

void Buzzer::begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
}

void Buzzer::ring() { digitalWrite(_pin, HIGH); }
void Buzzer::stop() { digitalWrite(_pin, LOW); }

void Buzzer::beep(int times) {
    for(int i=0; i<times; i++) {
        ring(); delay(200);
        stop(); delay(200);
    }
}