#include "GeminiESP32.h"

GeminiESP32::GeminiESP32(const char* token)
    :token(token){}

String GeminiESP32::askQuestion(String question,int Max_Token) {
    maxTokens=Max_Token;
    String res = String("\"") + question + String(" [tell me about this in shortest token possible with important detail info only]\"");
    HTTPClient https;
    String url = String("https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=") + token;

    if (https.begin(url)) {
        https.addHeader("Content-Type", "application/json");
        String payload = String("{\"contents\": [{\"parts\":[{\"text\":") + res + "}]}],\"generationConfig\": {\"maxOutputTokens\": " + String(maxTokens) + "}}";

        int httpCode = https.POST(payload);

        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String payload = https.getString();
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, payload);
            String answer = doc["candidates"][0]["content"]["parts"][0]["text"];
            answer.trim();

            String filteredAnswer = "";
            for (size_t i = 0; i < answer.length(); i++) {
                char c = answer[i];
                if (isalnum(c) || isspace(c)) {
                    filteredAnswer += c;
                } else {
                    filteredAnswer += ' ';
                }
            }
            https.end();
            return filteredAnswer;
        } else {
          https.end();
          Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
            return "Error";
        }
        https.end();
    } else {
      https.end();
      Serial.printf("[HTTPS] Unable to connect GEMINI\n");
      return "Error";
    }
}
