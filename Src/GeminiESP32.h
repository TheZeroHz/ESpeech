#ifndef GEMINI_ESP32_H
#define GEMINI_ESP32_H
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h> //dependency
class GeminiESP32 {
public:
    GeminiESP32(const char* token);
    String askQuestion(String question,int Max_Token=300);
private:
    const char* token;
    int maxTokens;
};

#endif
