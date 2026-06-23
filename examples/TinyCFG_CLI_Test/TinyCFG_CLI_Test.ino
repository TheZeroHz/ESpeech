/**
 * TinyCFG CLI Test Harness — Serial-only CFG evaluation (NO STT/TTS)
 *
 * Shows the FULL TinyCFG pipeline step-by-step on every parse:
 *   DATA FLOW diagram (input -> output at each stage)
 *   STEP 1 Input -> 2 Tokenization -> 3 Lexical lookup -> 4 Pattern match
 *   -> 5 Task tree -> 6 CDA -> 7 Metrics -> 8 Semantic dispatch
 *
 * Upload to ESP32, open Serial Monitor @ 115200.
 *
 * Commands:
 *   help              Show CLI reference
 *   info              Grammar & memory info
 *   parse <text>      Full pipeline trace + result
 *   tokens <text>     Step 2 only (tokenization)
 *   test              Run built-in test suite
 *   bench             Latency benchmark
 *   examples          Sample commands
 *   fuzzy on|off      Toggle edit-distance recovery
 *   phonetic on|off   Toggle PER (phonetic error recovery)
 *   trace on|off      Toggle pipeline trace (default ON)
 *
 * Direct input (without "parse" prefix) also runs full pipeline trace.
 */

#include <TinyCFG.h>

TinyCFG parser;
static bool showTrace = true;

struct TestCase {
    const char* input;
    bool shouldPass;
    const char* expectAction;
};

static const TestCase TEST_SUITE[] = {
    {"turn on room light and come here", true, "LIGHT_ON"},
    {"turn on kitchen fan", true, "FAN_ON"},
    {"turn on rakibs room fan", true, "FAN_ON"},
    {"turn off john bedroom fan then turn on kitchen light", true, "FAN_OFF"},
    {"switch on bedroom lamp then go to kitchen", true, "LIGHT_ON"},
    {"turn off kitchen light and lock front door", true, "LIGHT_OFF"},
    {"unlock rakibs front door", true, "UNLOCK"},
    {"open bedroom curtain and close living curtain", true, "CURTAIN_OPEN"},
    {"come here", true, "COME_HERE"},
    {"go to living room then follow me", true, "NAV_GO"},
    {"go to sarahs office", true, "NAV_GO"},
    {"patrol hallway and go home", true, "PATROL"},
    {"play music and volume up", true, "MEDIA_PLAY"},
    {"pause music then next song", true, "MEDIA_PAUSE"},
    {"arm alarm", true, "ALARM_ARM"},
    {"disarm alarm then check cameras", true, "ALARM_DISARM"},
    {"check garage camera", true, "CAMERA_VIEW"},
    {"scene movie and turn on living light", true, "SCENE"},
    {"set timer for five minutes then turn off room fan", true, "TIMER_SET"},
    {"heat up and cool down", true, "TEMP_UP"},
    {"dock", true, "NAV_DOCK"},
    {"stop", true, "STOP"},
    {"turn on lite and com here", true, "LIGHT_ON"},
    {"turn adjfija on room light", true, "LIGHT_ON"},
    {"TurnOnRoomLight", true, "LIGHT_ON"},
    {"tTurnOnRoomLight", true, "LIGHT_ON"},
    {"turnonroomlight", true, "LIGHT_ON"},
    {"120 turnonfan and after this turnoffroomlight", true, "FAN_ON"},
    {"turn on room light and turn off room fan", true, "LIGHT_ON"},
    {"turn on room light or turn off room fan", true, "LIGHT_ON"},
    {"turn on room light turn off room fan", true, "LIGHT_ON"},
    {"random gibberish words", false, ""},
};
static const uint8_t TEST_COUNT = sizeof(TEST_SUITE) / sizeof(TEST_SUITE[0]);

void printBanner() {
    Serial.println();
    Serial.println("=================================================");
    Serial.println("  TinyCFG CLI — Full Pipeline + Data Flow Trace");
    Serial.println("  Pattern-Augmented LL(1) + PER + DGS + CDA");
    Serial.println("=================================================");
    Serial.printf("Grammar: %s v%u | Patterns: %u\n",
        TinyCFG::getGrammarName(), TinyCFG::getGrammarVersion(), TinyCFG::getPatternCount());
    Serial.println("Type 'help' for commands. Every parse shows data flow + 8 steps.");
    Serial.println();
}

void printHelp() {
    Serial.println("--- CLI Commands ---");
    Serial.println("  help              This help");
    Serial.println("  info              Grammar metadata");
    Serial.println("  parse <sentence>  Run data flow + full 8-step pipeline trace");
    Serial.println("  flow <sentence>    Data flow diagram only (stages 0-9)");
    Serial.println("  tokens <sentence> Step 2 only (tokenization detail)");
    Serial.println("  test              Run test suite (compact output)");
    Serial.println("  bench             Benchmark parse latency");
    Serial.println("  examples          Sample commands");
    Serial.println("  fuzzy on|off      Edit-distance recovery");
    Serial.println("  phonetic on|off   Phonetic error recovery (PER)");
    Serial.println("  trace on|off      Show/hide pipeline trace (default ON)");
    Serial.println();
    Serial.println("Data flow + pipeline on each parse:");
    Serial.println("  0-9 Data flow  |  1 Input  2 Tokenize  3 Lexical  4 Pattern");
    Serial.println("  5 Task tree  6 CDA  7 Metrics  8 Dispatch");
    Serial.println();
    Serial.println("Or type any sentence directly.");
}

void printExamples() {
    Serial.println("--- Sample Commands ---");
    Serial.println("  turn on room light and turn off room fan");
    Serial.println("  turn on room light or turn off room fan");
    Serial.println("  turn on room light turn off room fan");
    Serial.println("  turn on rakibs room fan");
    Serial.println("  TurnOnRoomLight");
    Serial.println("  120 turnonfan and after this turnoffroomlight");
    Serial.println("  turn on lite and com here");
}

void printInfo() {
    Serial.println("--- TinyCFG System Info ---");
    Serial.printf("Grammar:     %s v%u\n", TinyCFG::getGrammarName(), TinyCFG::getGrammarVersion());
    Serial.printf("Patterns:    %u compiled command patterns\n", TinyCFG::getPatternCount());
    Serial.printf("Domains:     smarthome, robot, media, security, climate, timer\n");
    Serial.printf("Operators:   and, or, then, after, before, if, until, while, (back-to-back)\n");
    Serial.printf("Parser:      LL(1) recursive descent + pattern table\n");
    Serial.printf("Recovery:    PER + fuzzy + DGS deglue + gap-tolerant match\n");
    Serial.printf("Analysis:    CDA (Command Dependency Analyzer)\n");
    Serial.printf("sizeof(TinyCFG): %u bytes\n", (unsigned)sizeof(TinyCFG));
}

void onAction(uint8_t actionId, const char* arg, uint8_t domain, void*) {
    Serial.printf("    >> EXEC [%s] %s(%s)\n",
        TinyCFG::domainName(domain), TinyCFG::actionName(actionId), arg);
}

void runParse(const String& text) {
    Serial.printf("\n>> INPUT: \"%s\"\n", text.c_str());

    bool ok = parser.parse(text);

    if (showTrace) {
        parser.printPipeline(Serial);
    } else if (ok) {
        parser.printTokens(Serial);
        parser.printTaskTree(Serial);
        parser.printDependencyReport(Serial);
        parser.printMetrics(Serial);
        Serial.println(">> Semantic dispatch:");
        parser.executeActions();
    } else {
        parser.printLexicalDetail(Serial);
        parser.printMetrics(Serial);
    }

    Serial.printf(">> FINAL RESULT: %s\n\n", ok ? "PARSE OK" : "PARSE FAILED");
}

void runFlowOnly(const String& text) {
    Serial.printf("\n>> DATA FLOW: \"%s\"\n", text.c_str());
    parser.parse(text);
    parser.printDataFlow(Serial);
    Serial.println();
}

void runTokensOnly(const String& text) {
    Serial.printf("\n>> TOKENIZE ONLY: \"%s\"\n", text.c_str());
    parser.reset();
    parser.tokenizeOnly(text.c_str());

    Serial.println();
    parser.printDataFlow(Serial);
    Serial.println();
    Serial.println("[STEP 2] TOKENIZATION (detail)");
    parser.printLexicalDetail(Serial);
    Serial.println();
    Serial.println("[STEP 3] LEXICAL SETTINGS");
    parser.printRecoveryConfig(Serial);
    Serial.printf("Tokens: %u\n\n", parser.getStats().tokensTotal);
}

void runTestSuite() {
    Serial.println("--- Running TinyCFG Test Suite (compact) ---");
    bool savedTrace = showTrace;
    showTrace = false;

    uint8_t passed = 0;
    uint8_t failed = 0;

    for (uint8_t i = 0; i < TEST_COUNT; ++i) {
        const TestCase& tc = TEST_SUITE[i];
        bool ok = parser.parse(tc.input);
        bool pass = (ok == tc.shouldPass);

        if (pass && ok && tc.expectAction[0]) {
            bool found = false;
            const TcfgTaskNode* root = parser.getTaskTree();
            if (root) {
                for (uint8_t c = 0; c < root->childCount; ++c) {
                    if (root->children[c]->type == TCFG_NODE_ACTION) {
                        const char* name = TinyCFG::actionName(root->children[c]->actionId);
                        if (strstr(name, tc.expectAction)) { found = true; break; }
                    }
                }
            }
            pass = found;
        }

        Serial.printf("  [%c] #%02u: \"%s\"\n", pass ? 'P' : 'F', i + 1, tc.input);
        if (pass) passed++; else failed++;
    }

    showTrace = savedTrace;
    Serial.printf("\nResults: %u/%u passed, %u failed\n", passed, TEST_COUNT, failed);
    Serial.printf("Pass rate: %.1f%%\n\n", 100.0f * passed / TEST_COUNT);
}

void runBench() {
    Serial.println("--- TinyCFG Latency Benchmark ---");
    bool savedTrace = showTrace;
    showTrace = false;

    uint32_t totalUs = 0;
    uint32_t minUs = UINT32_MAX;
    uint32_t maxUs = 0;

    for (uint8_t i = 0; i < TEST_COUNT; ++i) {
        parser.parse(TEST_SUITE[i].input);
        uint32_t us = parser.getStats().parseLatencyUs;
        totalUs += us;
        if (us < minUs) minUs = us;
        if (us > maxUs) maxUs = us;
    }

    showTrace = savedTrace;
    Serial.printf("Cases:   %u\n", TEST_COUNT);
    Serial.printf("Avg:     %u us\n", totalUs / TEST_COUNT);
    Serial.printf("Min:     %u us\n", minUs);
    Serial.printf("Max:     %u us\n", maxUs);
    Serial.printf("RAM:     %u bytes (parser instance)\n\n", (unsigned)sizeof(TinyCFG));
}

void handleCommand(String line) {
    line.trim();
    if (line.length() == 0) return;

    if (line.equalsIgnoreCase("help")) { printHelp(); return; }
    if (line.equalsIgnoreCase("info")) { printInfo(); return; }
    if (line.equalsIgnoreCase("test")) { runTestSuite(); return; }
    if (line.equalsIgnoreCase("bench")) { runBench(); return; }
    if (line.equalsIgnoreCase("examples")) { printExamples(); return; }

    if (line.startsWith("fuzzy ")) {
        bool on = line.endsWith("on");
        parser.setFuzzyMatch(on);
        Serial.printf("Fuzzy match: %s\n", on ? "ON" : "OFF");
        return;
    }
    if (line.startsWith("phonetic ")) {
        bool on = line.endsWith("on");
        parser.setPhoneticRecovery(on);
        Serial.printf("Phonetic recovery (PER): %s\n", on ? "ON" : "OFF");
        return;
    }
    if (line.startsWith("trace ")) {
        showTrace = line.endsWith("on");
        Serial.printf("Pipeline trace: %s\n", showTrace ? "ON" : "OFF");
        return;
    }

    if (line.startsWith("flow ")) {
        runFlowOnly(line.substring(5));
        return;
    }
    if (line.startsWith("parse ")) {
        runParse(line.substring(6));
        return;
    }
    if (line.startsWith("tokens ")) {
        runTokensOnly(line.substring(7));
        return;
    }

    runParse(line);
}

void setup() {
    Serial.begin(115200);
    delay(800);
    printBanner();
    parser.setFuzzyMatch(true);
    parser.setPhoneticRecovery(true);
    parser.setErrorRecovery(true);
    parser.setActionHandler(onAction);
    showTrace = true;
}

void loop() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    handleCommand(line);
}
