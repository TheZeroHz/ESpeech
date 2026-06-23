/**
 * TinyCFG + ESpeech Integration Example
 *
 * Full pipeline:
 *   Voice -> STT (ESpeech) -> TinyCFG Parser -> Task Tree -> CDA -> Robot Actions
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include <ESpeech.h>
#include <TinyCFG.h>

ESpeech STT(I2S_NUM_1, I2S_SCK, I2S_WS, I2S_SD);
TinyCFG parser;

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
#define serverUrl "https://espeechserver-iukg.onrender.com/uploadAudio"

void onRobotAction(uint8_t actionId, const char* arg, uint8_t domain, void*) {
    Serial.printf("  -> [%s] %s(%s)\n",
        TinyCFG::domainName(domain), TinyCFG::actionName(actionId), arg);
}

void setup() {
    Serial.begin(115200);
    Serial.println("TinyCFG + ESpeech Voice Command Pipeline");

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    STT.serverURL(serverUrl);
    parser.setFuzzyMatch(true);
    parser.setPhoneticRecovery(true);
    parser.setErrorRecovery(true);
    parser.setActionHandler(onRobotAction);

    Serial.println("Send 'start' to record, or type a command directly.");
}

void processCommand(const String& text) {
    Serial.printf("STT/Text: \"%s\"\n", text.c_str());

    if (parser.parse(text)) {
        parser.printTaskTree(Serial);
        parser.printDependencyReport(Serial);
        parser.printMetrics(Serial);
        Serial.println("Semantic Actions:");
        parser.executeActions();
    } else {
        Serial.println("Could not parse command. Try rephrasing.");
    }
}

void loop() {
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.equalsIgnoreCase("start")) {
        Serial.println("Recording...");
        STT.recordAudio();
        processCommand(STT.getTranscription());
    } else if (cmd.length() > 0) {
        processCommand(cmd);
    }
}
