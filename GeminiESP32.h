#ifndef GEMINI_ESP32_H
#define GEMINI_ESP32_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

class GeminiESP32 {
public:
    GeminiESP32(const char* ssid, const char* password, const char* token, int maxTokens = 300);
    void begin();
    String askQuestion(String question);

private:
    const char* ssid;
    const char* password;
    const char* token;
    int maxTokens;
    void connectWiFi();
};

#endif
