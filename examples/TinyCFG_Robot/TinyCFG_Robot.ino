/**
 * TinyCFG Robot Example (standalone)
 *
 * Demonstrates the offline-compiled LL(1) grammar parser without STT.
 * Type voice commands over Serial to see the hierarchical task tree.
 *
 * Example: turn on room light and come here
 */

#include <TinyCFG.h>

TinyCFG parser;

void onRobotAction(uint8_t actionId, const char* arg, void* userData) {
    (void)userData;
    switch (actionId) {
        case TCFG_ACT_LIGHT_ON:
            Serial.printf("[EXEC] Room Light ON -> %s\n", arg);
            break;
        case TCFG_ACT_LIGHT_OFF:
            Serial.printf("[EXEC] Light OFF -> %s\n", arg);
            break;
        case TCFG_ACT_COME_HERE:
            Serial.println("[EXEC] Robot navigating to user (COME_HERE)");
            break;
        case TCFG_ACT_STOP:
            Serial.println("[EXEC] Robot STOP");
            break;
        default:
            Serial.printf("[EXEC] Unknown action id=%u\n", actionId);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("=== TinyCFG Robot Parser ===");
    Serial.println("Grammar: robot.cfg (LL(1), compiled offline)");
    Serial.println("Try: turn on room light and come here");
    Serial.println("     switch on bedroom lamp then go me");
    Serial.println("     halt");
    Serial.println();

    parser.setFuzzyMatch(true);
    parser.setErrorRecovery(true);
    parser.setActionHandler(onRobotAction);
}

void loop() {
    if (!Serial.available()) return;

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    Serial.printf("\nInput: \"%s\"\n", line.c_str());

    if (parser.parse(line)) {
        Serial.println("Parse: OK");
        Serial.println("Task Tree:");
        parser.printTaskTree(Serial);

        const TcfgParseStats& s = parser.getStats();
        Serial.printf("Confidence: %.0f%%  (tokens %u/%u, recoveries %u)\n",
            s.confidence * 100.0f, s.tokensConsumed, s.tokensTotal, s.recoveryCount);

        Serial.println("Executing semantic actions:");
        parser.executeActions();
    } else {
        Serial.println("Parse: FAILED (no valid task recognized)");
    }
    Serial.println();
}
