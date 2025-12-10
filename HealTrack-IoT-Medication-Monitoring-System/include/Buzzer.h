#ifndef BUZZER_H
#define BUZZER_H
#include <Arduino.h>
class Buzzer {
private:
    uint8_t _pin;
public:
    Buzzer(uint8_t pin);
    void begin();
    void ring();
    void stop();
    void beep(int duration);
};
#endif
