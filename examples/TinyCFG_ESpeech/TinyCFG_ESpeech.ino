/**
 * TinyCFG + ESpeech Integration Example
 *
 * Full pipeline from the TinyCFG architecture diagram:
 *   Voice -> STT (ESpeech) -> Tokenizer -> TinyCFG Parser -> Task Tree -> Robot Actions
 *
 * Configure WiFi and server URL, then send "start" over Serial to record speech.
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

void onRobotAction(uint8_t actionId, const char* arg, void* userData) {
    (void)userData;
    switch (actionId) {
        case TCFG_ACT_LIGHT_ON:
            Serial.printf("  -> LIGHT_ON(%s)\n", arg);
            // digitalWrite(ROOM_LIGHT_PIN, HIGH);
            break;
        case TCFG_ACT_LIGHT_OFF:
            Serial.printf("  -> LIGHT_OFF(%s)\n", arg);
            break;
        case TCFG_ACT_COME_HERE:
            Serial.println("  -> COME_HERE()");
            break;
        case TCFG_ACT_STOP:
            Serial.println("  -> STOP()");
            break;
        default:
            break;
    }
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
    parser.setErrorRecovery(true);
    parser.setActionHandler(onRobotAction);

    Serial.println("Send 'start' to record, or type a command directly.");
}

void processCommand(const String& text) {
    Serial.printf("STT/Text: \"%s\"\n", text.c_str());

    if (parser.parse(text)) {
        Serial.println("Task Tree:");
        parser.printTaskTree(Serial);
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
        String transcription = STT.getTranscription();
        processCommand(transcription);
    } else if (cmd.length() > 0) {
        processCommand(cmd);
    }
}
