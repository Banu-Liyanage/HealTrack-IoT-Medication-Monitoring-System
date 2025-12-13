#include "Display.h"

Display::Display() {
    _oled = new Adafruit_SSD1306(128, 64, &Wire, -1);
}

void Display::begin() {
    if(!_oled->begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("SSD1306 allocation failed"));
    }
    _oled->clearDisplay();
    _oled->setTextColor(WHITE);
}

void Display::clear() { _oled->clearDisplay(); }
void Display::display() { _oled->display(); }

// --- Dashboard ---
void Display::showStartup() {
    _oled->clearDisplay();
    _oled->setTextSize(2);
    _oled->setCursor(10, 20);
    _oled->println("HealTrack");
    _oled->setTextSize(1);
    _oled->setCursor(30, 45);
    _oled->println("System Init");
    _oled->display();
}

void Display::updateDashboard(float temp, float humidity, float weight, float light, String status) {
    _oled->clearDisplay();
    _oled->setTextSize(1);
    _oled->setCursor(0, 0); _oled->print("HealTrack");
    _oled->setCursor(80, 0); _oled->print(status);

    _oled->setCursor(0, 15); _oled->printf("T:%.1fC  H:%.0f%%", temp, humidity);
    _oled->setCursor(0, 25); _oled->printf("L:%.2f", light);
    _oled->setCursor(0, 35); _oled->printf("W:%.1fg", weight);

    _oled->setCursor(0, 50); _oled->print("[OK] for Menu");
    _oled->display();
}

void Display::showAlarmScreen() {
    _oled->clearDisplay();
    _oled->setTextSize(2);
    _oled->setCursor(20, 10); _oled->println("ALARM!");
    _oled->setTextSize(1);
    _oled->setCursor(10, 40); _oled->println("Take Meds or Snooze");
    _oled->display();
}

// --- Menu Helpers ---
void Display::printLine(String text, int size, int row, int col) {
    _oled->setTextSize(size);
    _oled->setTextColor(SSD1306_WHITE);
    _oled->setCursor(col, row);
    _oled->println(text);
}

void Display::printLineInverted(String text, int size, int row, int col) {
    _oled->setTextSize(size);
    _oled->setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Inverted
    _oled->setCursor(col, row);
    _oled->println(text);
}

void Display::drawMenu(String options[], int currentSelection, int totalOptions) {
    _oled->clearDisplay();
    for(int i=0; i<totalOptions; i++) {
        int row = i * 12;
        if(i == currentSelection) {
            printLineInverted(options[i], 1, row, 0);
        } else {
            printLine(options[i], 1, row, 0);
        }
    }
    _oled->display();
}

void Display::drawTimeSet(int hour, int minute, String label) {
    _oled->clearDisplay();
    printLine(label, 1, 0, 0);
    _oled->setTextSize(2);
    _oled->setCursor(20, 25);
    if(hour < 10) _oled->print("0");
    _oled->print(hour);
    _oled->print(":");
    if(minute < 10) _oled->print("0");
    _oled->print(minute);
    _oled->display();
}