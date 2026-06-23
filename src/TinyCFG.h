#ifndef TINYCFG_H
#define TINYCFG_H

#include <Arduino.h>
#include "TinyCFGTypes.h"
#include "TinyCFGGrammar.h"
#include "TinyCFGDependency.h"

typedef void (*TcfgActionHandler)(uint8_t actionId, const char* arg, uint8_t domain, void* userData);

/**
 * TinyCFG v2 — Lightweight deterministic CFG parser for embedded N-intent voice control.
 *
 * Novel contributions (conference paper):
 *   1. Pattern-augmented LL(1): CFG composition + compiled terminal patterns
 *   2. PER: Phonetic Error Recovery via Soundex-like synonym keys
 *   3. CDA: Command Dependency Analyzer with resource conflict detection
 *   4. Multi-domain grammars: smarthome, robot, media, security, climate, timer
 */
class TinyCFG {
public:
    TinyCFG();

    void setFuzzyMatch(bool enable);
    void setPhoneticRecovery(bool enable);
    void setErrorRecovery(bool enable);
    void setActionHandler(TcfgActionHandler handler, void* userData = nullptr);

    bool parse(const char* text);
    bool parse(const String& text);
    void tokenizeOnly(const char* text);

    const TcfgTaskNode* getTaskTree() const;
    const TcfgParseStats& getStats() const;
    const TcfgDependencyReport& getDependencyReport() const;

    void printTokens(Stream& out) const;
    void printLexicalDetail(Stream& out) const;
    void printTaskTree(Stream& out) const;
    void printDependencyReport(Stream& out) const;
    void printMetrics(Stream& out) const;
    void printRecoveryConfig(Stream& out) const;
    void printActionsSummary(Stream& out) const;
    void printPipeline(Stream& out) const;
    void printDataFlow(Stream& out) const;
    const char* getDataFlowStage(uint8_t stage) const;

    void setVerbose(bool enable);
    bool isVerbose() const;

    void executeActions() const;
    void dispatchActions(Stream& out) const;
    void reset();

    static const char* actionName(uint8_t actionId);
    static const char* domainName(uint8_t domainId);
    static const char* seqOpName(TcfgSeqOp op);

    static uint8_t getPatternCount();
    static const char* getGrammarName();
    static uint8_t getGrammarVersion();

private:
    uint8_t tokenBuf[TCFG_MAX_TOKENS];
    char tokenText[TCFG_MAX_TOKENS][TCFG_MAX_TOKEN_TEXT];
    uint8_t tokenFlags[TCFG_MAX_TOKENS];
    uint8_t tokenCount;
    uint8_t cursor;

    TcfgTaskNode pool[TCFG_MAX_TASKS];
    uint8_t poolUsed;
    TcfgTaskNode* root;

    TcfgParseStats stats;
    TcfgDependencyReport depReport;

    bool fuzzyMatch;
    bool phoneticRecovery;
    bool errorRecovery;
    bool verbose;
    TcfgActionHandler actionHandler;
    void* handlerUserData;
    char lastInput[128];

    // Data-flow snapshots (input -> output transformation trace)
    char flowNormalized[128];
    char flowSpacedWords[192];
    char flowDgsNotes[160];
    char flowTokenStream[256];
    char flowLexicalMap[320];
    char flowPatternMatch[256];
    char flowTreeLine[256];
    char flowCdaLine[128];
    char flowOutput[256];
    uint8_t flowPreGlueCount;
    char flowPreGlue[TCFG_MAX_TOKENS][TCFG_MAX_TOKEN_TEXT];

    void capturePreGlueTokens();
    void buildDataFlow();
    void buildNormalizedPreview(const char* text, char* out, size_t outLen) const;
    void joinWords(const char words[][TCFG_MAX_TOKEN_TEXT], uint8_t count,
                   char* out, size_t outLen) const;
    const char* terminalLabel(uint8_t tok) const;
    static void strAppend(char* buf, size_t bufLen, const char* piece);

    void tokenize(const char* text);
    void tokenizeSpaced(const char* text);
    void tokenizeGlued(const char* text);
    void pushToken(const char* word, uint8_t flags);
    bool needsGluedSegmentation(const char* text) const;
    void expandCamelCase(const char* in, char* out, size_t outLen) const;
    void expandInlineGluedTokens();
    bool wordNeedsGluedSegmentation(const char* word) const;
    uint8_t segmentGluedWord(const char* text, char out[][TCFG_MAX_TOKEN_TEXT], uint8_t maxOut) const;
    static bool isPureDigits(const char* word);
    static bool isFillerWord(const char* word);
    uint8_t countActionNodes(const TcfgTaskNode* list) const;
    uint8_t lookupToken(const char* word, uint8_t* flagsOut = nullptr);
    uint8_t phoneticLookup(const char* word);
    uint8_t editDistanceLookup(const char* word) const;
    uint8_t peek(uint8_t offset = 0) const;
    bool atEnd() const;

    TcfgTaskNode* allocNode();
    void attachChild(TcfgTaskNode* parent, TcfgTaskNode* child);

    TcfgSeqOp peekOp() const;
    TcfgSeqOp consumeOp();
    bool isOpToken(uint8_t tok) const;

    bool matchPattern(uint8_t& actionId, char* argOut, uint8_t& domain, uint8_t& resource);
    bool tryMatchPatternAt(uint8_t patIdx, uint8_t startPos, uint8_t& endPos,
                           char* capLoc, char* capName, char* capDev, uint8_t& skips) const;
    bool isDigitNoise(uint8_t idx) const;
    bool isSkippableNoise(uint8_t idx) const;
    bool shouldSkipForPattern(uint8_t idx, uint8_t patTok, uint8_t slotType) const;
    bool isValidNameToken(uint8_t tok, uint8_t idx) const;
    bool isLocationToken(uint8_t tok) const;
    bool isDeviceToken(uint8_t tok) const;
    bool tokenMatchesPattern(uint8_t patTok, uint8_t slotType, uint8_t inputTok, uint8_t inputIdx) const;
    void buildArgFromTemplate(const char* tmpl, const char* loc, const char* name,
                              const char* device, char* out) const;
    bool parseTaskList(TcfgTaskNode* list);
    bool parseOpRest(TcfgTaskNode* list);
    bool parseSingleTask(TcfgTaskNode* list, TcfgSeqOp op);
    bool canStartCommandAt(uint8_t startPos) const;

    void skipNoise();
    void collectActions(const TcfgTaskNode* node, uint8_t* indices, uint8_t& count) const;
    void printNode(const TcfgTaskNode* node, Stream& out, int indent) const;
};

#endif
