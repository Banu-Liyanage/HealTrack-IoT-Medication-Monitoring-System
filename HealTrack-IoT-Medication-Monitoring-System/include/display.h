#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

class Display {
private:
    Adafruit_SSD1306* _oled;

public:
    Display();
    void begin();
    void clear();
    void display();
    
    // Dashboard Methods
    void showStartup();
    void updateDashboard(float temp, float humidity, float weight, float light, String status);
    void showAlarmScreen();
    
    // Menu Methods (New)
    void printLine(String text, int size, int row, int col);
    void printLineInverted(String text, int size, int row, int col);
    void drawMenu(String options[], int currentSelection, int totalOptions);
    void drawTimeSet(int hour, int minute, String label);
};

#endif