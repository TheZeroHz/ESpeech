/**
 * TinyCFG Robot Example (legacy — use TinyCFG_CLI_Test for full evaluation)
 */
#include <TinyCFG.h>

TinyCFG parser;

void onRobotAction(uint8_t actionId, const char* arg, uint8_t domain, void*) {
    Serial.printf("[EXEC] [%s] %s(%s)\n",
        TinyCFG::domainName(domain), TinyCFG::actionName(actionId), arg);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("TinyCFG — type commands (or use examples/TinyCFG_CLI_Test for full CLI)");
    parser.setActionHandler(onRobotAction);
}

void loop() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    if (parser.parse(line)) {
        parser.printTaskTree(Serial);
        parser.executeActions();
    } else {
        Serial.println("Parse failed.");
    }
}
