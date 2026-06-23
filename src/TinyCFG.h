#ifndef TINYCFG_H
#define TINYCFG_H

#include <Arduino.h>
#include "TinyCFGTypes.h"
#include "grammars/robot_grammar.h"

typedef void (*TcfgActionHandler)(uint8_t actionId, const char* arg, void* userData);

class TinyCFG {
public:
    TinyCFG();

    void setFuzzyMatch(bool enable);
    void setErrorRecovery(bool enable);
    void setActionHandler(TcfgActionHandler handler, void* userData = nullptr);

    bool parse(const char* text);
    bool parse(const String& text);

    const TcfgTaskNode* getTaskTree() const;
    const TcfgParseStats& getStats() const;

    void printTaskTree(Stream& out) const;
    void executeActions() const;
    void reset();

    static const char* actionName(uint8_t actionId);
    static const char* seqOpName(TcfgSeqOp op);

private:
    uint8_t tokenBuf[TCFG_MAX_TOKENS];
    uint8_t tokenCount;
    uint8_t cursor;

    TcfgTaskNode pool[TCFG_MAX_TASKS];
    uint8_t poolUsed;
    TcfgTaskNode* root;

    TcfgParseStats stats;
    bool fuzzyMatch;
    bool errorRecovery;
    TcfgActionHandler actionHandler;
    void* handlerUserData;

    void tokenize(const char* text);
    uint8_t lookupToken(const char* word) const;
    uint8_t peek() const;
    bool match(uint8_t tok);
    bool matchAny(const uint8_t* toks, uint8_t n);

    TcfgTaskNode* allocNode();
    TcfgSeqOp parseOp();
    bool parseTaskList(TcfgTaskNode* list);
    bool parseOpRest(TcfgTaskNode* list, TcfgSeqOp pendingOp);
    bool parseTask(TcfgTaskNode* parent, TcfgSeqOp op);
    bool parseLightOn(char* argOut);
    bool parseLightOff(char* argOut);
    bool parseComeHere();
    bool parseStop();
    bool parseObject(char* argOut);

    uint8_t fuzzyLookup(const char* word) const;
    void skipUnknown();
    void attachChild(TcfgTaskNode* parent, TcfgTaskNode* child);
    void printNode(const TcfgTaskNode* node, Stream& out, int indent) const;
};

#endif
