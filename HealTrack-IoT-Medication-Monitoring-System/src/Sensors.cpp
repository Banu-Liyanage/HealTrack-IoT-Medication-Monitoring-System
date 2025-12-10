#include "Sensors.h"

SensorManager::SensorManager(uint8_t dhtPin, uint8_t ldrPin, uint8_t dtPin, uint8_t sckPin) {
    _dhtPin = dhtPin;
    _ldrPin = ldrPin;
    _dtPin = dtPin;
    _sckPin = sckPin;
    
    // Instantiate objects dynamically
    _dht = new DHT(_dhtPin, DHT22);
    _scale = new HX711();
}

void SensorManager::begin() {
    pinMode(_ldrPin, INPUT);
    _dht->begin();
    _scale->begin(_dtPin, _sckPin);
    _scale->set_scale(420.0); // Calibration factor
    _scale->tare();
}

float SensorManager::readTemperature() {
    float t = _dht->readTemperature();
    return isnan(t) ? 0.0 : t;
}

float SensorManager::readHumidity() {
    float h = _dht->readHumidity();
    return isnan(h) ? 0.0 : h;
}

float SensorManager::readLightIntensity() {
    int raw = analogRead(_ldrPin);
    return (float)raw / 4063.0;
}

float SensorManager::readWeight() {
    if (_scale->is_ready()) {
        return _scale->get_units(5);
    }
    return 0.0;
}