#include "TinyCFG.h"
#include <ctype.h>
#include <string.h>

TinyCFG::TinyCFG()
    : tokenCount(0), cursor(0), poolUsed(0), root(nullptr),
      fuzzyMatch(true), errorRecovery(true),
      actionHandler(nullptr), handlerUserData(nullptr) {
    memset(&stats, 0, sizeof(stats));
}

void TinyCFG::setFuzzyMatch(bool enable) { fuzzyMatch = enable; }
void TinyCFG::setErrorRecovery(bool enable) { errorRecovery = enable; }

void TinyCFG::setActionHandler(TcfgActionHandler handler, void* userData) {
    actionHandler = handler;
    handlerUserData = userData;
}

void TinyCFG::reset() {
    tokenCount = 0;
    cursor = 0;
    poolUsed = 0;
    root = nullptr;
    memset(&stats, 0, sizeof(stats));
}

const TcfgTaskNode* TinyCFG::getTaskTree() const { return root; }
const TcfgParseStats& TinyCFG::getStats() const { return stats; }

bool TinyCFG::parse(const String& text) { return parse(text.c_str()); }

bool TinyCFG::parse(const char* text) {
    reset();
    if (!text || !text[0]) return false;

    tokenize(text);
    stats.tokensTotal = tokenCount;
    if (tokenCount == 0) return false;

    root = allocNode();
    root->type = TCFG_NODE_TASK_LIST;

    bool ok = parseTaskList(root);
    stats.tokensConsumed = cursor;
    stats.confidence = tokenCount > 0
        ? (float)(tokenCount - stats.recoveryCount) / (float)tokenCount
        : 0.0f;

    return ok && root->childCount > 0;
}

void TinyCFG::tokenize(const char* text) {
    char word[32];
    uint8_t wi = 0;

    for (const char* p = text; *p; ++p) {
        char c = (char)tolower((unsigned char)*p);
        if (isalnum((unsigned char)c)) {
            if (wi < sizeof(word) - 1) word[wi++] = c;
        } else if (wi > 0) {
            word[wi] = '\0';
            if (tokenCount < TCFG_MAX_TOKENS) {
                tokenBuf[tokenCount++] = lookupToken(word);
            }
            wi = 0;
        }
    }
    if (wi > 0) {
        word[wi] = '\0';
        if (tokenCount < TCFG_MAX_TOKENS) {
            tokenBuf[tokenCount++] = lookupToken(word);
        }
    }
}

uint8_t TinyCFG::lookupToken(const char* word) const {
    for (uint8_t i = 0; i < TCFG_SYNONYM_COUNT; ++i) {
        if (strcmp(word, TCFG_SYNONYMS[i].word) == 0) {
            return TCFG_SYNONYMS[i].terminal;
        }
    }
    if (fuzzyMatch) return fuzzyLookup(word);
    return TCFG_TOK_UNKNOWN;
}

uint8_t TinyCFG::fuzzyLookup(const char* word) const {
    uint8_t best = TCFG_TOK_UNKNOWN;
    int bestDist = 3;

    for (uint8_t i = 0; i < TCFG_SYNONYM_COUNT; ++i) {
        const char* cand = TCFG_SYNONYMS[i].word;
        int lenW = (int)strlen(word);
        int lenC = (int)strlen(cand);
        if (abs(lenW - lenC) > 2) continue;

        int dist = 0;
        int maxLen = lenW > lenC ? lenW : lenC;
        for (int j = 0; j < maxLen; ++j) {
            char a = j < lenW ? word[j] : '\0';
            char b = j < lenC ? cand[j] : '\0';
            if (a != b) dist++;
        }
        if (dist < bestDist) {
            bestDist = dist;
            best = TCFG_SYNONYMS[i].terminal;
        }
    }
    return best;
}

uint8_t TinyCFG::peek() const {
    if (cursor >= tokenCount) return TCFG_TOK_EOF;
    return tokenBuf[cursor];
}

bool TinyCFG::match(uint8_t tok) {
    if (peek() == tok) {
        cursor++;
        return true;
    }
    if (errorRecovery && tok != TCFG_TOK_EOF) {
        stats.recoveryCount++;
        return false;
    }
    return false;
}

bool TinyCFG::matchAny(const uint8_t* toks, uint8_t n) {
    uint8_t p = peek();
    for (uint8_t i = 0; i < n; ++i) {
        if (p == toks[i]) {
            cursor++;
            return true;
        }
    }
    return false;
}

TcfgTaskNode* TinyCFG::allocNode() {
    if (poolUsed >= TCFG_MAX_TASKS) return nullptr;
    TcfgTaskNode* n = &pool[poolUsed++];
    memset(n, 0, sizeof(TcfgTaskNode));
    return n;
}

void TinyCFG::attachChild(TcfgTaskNode* parent, TcfgTaskNode* child) {
    if (!parent || !child) return;
    if (parent->childCount < TCFG_MAX_TASKS) {
        parent->children[parent->childCount++] = child;
    }
}

TcfgSeqOp TinyCFG::parseOp() {
    uint8_t p = peek();
    if (p == TCFG_TOK_AND)   { cursor++; return TCFG_SEQ_AND; }
    if (p == TCFG_TOK_THEN)  { cursor++; return TCFG_SEQ_THEN; }
    if (p == TCFG_TOK_AFTER) { cursor++; return TCFG_SEQ_AFTER; }
    if (p == TCFG_TOK_BEFORE){ cursor++; return TCFG_SEQ_BEFORE; }
    if (p == TCFG_TOK_IF)    { cursor++; return TCFG_SEQ_IF; }
    if (p == TCFG_TOK_UNTIL) { cursor++; return TCFG_SEQ_UNTIL; }
    return TCFG_SEQ_NONE;
}

bool TinyCFG::parseObject(char* argOut) {
    uint8_t p = peek();

    if (p == TCFG_TOK_ROOM && cursor + 1 < tokenCount && tokenBuf[cursor + 1] == TCFG_TOK_LIGHT) {
        cursor += 2;
        strncpy(argOut, "room_light", TCFG_MAX_ARG_LEN - 1);
        return true;
    }
    if (p == TCFG_TOK_BEDROOM && cursor + 1 < tokenCount && tokenBuf[cursor + 1] == TCFG_TOK_LIGHT) {
        cursor += 2;
        strncpy(argOut, "bedroom_light", TCFG_MAX_ARG_LEN - 1);
        return true;
    }
    if (p == TCFG_TOK_LIVING && cursor + 1 < tokenCount && tokenBuf[cursor + 1] == TCFG_TOK_LIGHT) {
        cursor += 2;
        strncpy(argOut, "living_light", TCFG_MAX_ARG_LEN - 1);
        return true;
    }
    if (p == TCFG_TOK_KITCHEN && cursor + 1 < tokenCount && tokenBuf[cursor + 1] == TCFG_TOK_LIGHT) {
        cursor += 2;
        strncpy(argOut, "kitchen_light", TCFG_MAX_ARG_LEN - 1);
        return true;
    }
    if (p == TCFG_TOK_LIGHT) {
        cursor++;
        strncpy(argOut, "default_light", TCFG_MAX_ARG_LEN - 1);
        return true;
    }
    return false;
}

bool TinyCFG::parseLightOn(char* argOut) {
    if (!match(TCFG_TOK_TURN)) return false;
    if (!match(TCFG_TOK_ON)) {
        if (errorRecovery) { stats.recoveryCount++; skipUnknown(); }
        else return false;
    }
    if (!parseObject(argOut)) {
        strncpy(argOut, "default_light", TCFG_MAX_ARG_LEN - 1);
    }
    return true;
}

bool TinyCFG::parseLightOff(char* argOut) {
    if (!match(TCFG_TOK_TURN)) return false;
    if (!match(TCFG_TOK_OFF)) return false;
    if (!parseObject(argOut)) {
        strncpy(argOut, "default_light", TCFG_MAX_ARG_LEN - 1);
    }
    return true;
}

bool TinyCFG::parseComeHere() {
    if (!match(TCFG_TOK_COME)) return false;
    if (!match(TCFG_TOK_HERE)) {
        if (errorRecovery) stats.recoveryCount++;
    }
    return true;
}

bool TinyCFG::parseStop() {
    return match(TCFG_TOK_STOP);
}

bool TinyCFG::parseTask(TcfgTaskNode* parent, TcfgSeqOp op) {
    uint8_t saved = cursor;
    char arg[TCFG_MAX_ARG_LEN] = {0};

    if (peek() == TCFG_TOK_TURN && cursor + 1 < tokenCount) {
        if (tokenBuf[cursor + 1] == TCFG_TOK_ON) {
            if (!parseLightOn(arg)) return false;
            TcfgTaskNode* action = allocNode();
            action->type = TCFG_NODE_ACTION;
            action->actionId = TCFG_ACT_LIGHT_ON;
            action->seqOp = op;
            strncpy(action->arg, arg, TCFG_MAX_ARG_LEN - 1);
            attachChild(parent, action);
            return true;
        }
        if (tokenBuf[cursor + 1] == TCFG_TOK_OFF) {
            if (!parseLightOff(arg)) return false;
            TcfgTaskNode* action = allocNode();
            action->type = TCFG_NODE_ACTION;
            action->actionId = TCFG_ACT_LIGHT_OFF;
            action->seqOp = op;
            strncpy(action->arg, arg, TCFG_MAX_ARG_LEN - 1);
            attachChild(parent, action);
            return true;
        }
    }

    if (peek() == TCFG_TOK_COME) {
        if (!parseComeHere()) return false;
        TcfgTaskNode* action = allocNode();
        action->type = TCFG_NODE_ACTION;
        action->actionId = TCFG_ACT_COME_HERE;
        action->seqOp = op;
        attachChild(parent, action);
        return true;
    }

    if (peek() == TCFG_TOK_STOP) {
        if (!parseStop()) return false;
        TcfgTaskNode* action = allocNode();
        action->type = TCFG_NODE_ACTION;
        action->actionId = TCFG_ACT_STOP;
        action->seqOp = op;
        attachChild(parent, action);
        return true;
    }

    cursor = saved;
    if (errorRecovery) {
        stats.recoveryCount++;
        skipUnknown();
        return false;
    }
    return false;
}

void TinyCFG::skipUnknown() {
    if (cursor < tokenCount && tokenBuf[cursor] == TCFG_TOK_UNKNOWN) {
        cursor++;
    }
}

bool TinyCFG::parseOpRest(TcfgTaskNode* list, TcfgSeqOp) {
    TcfgSeqOp op = parseOp();
    if (op == TCFG_SEQ_NONE) return true;

    TcfgTaskNode* opNode = allocNode();
    opNode->type = TCFG_NODE_OPERATOR;
    opNode->seqOp = op;
    attachChild(list, opNode);

    if (!parseTask(list, op)) {
        if (!errorRecovery) return false;
    }
    return parseOpRest(list, op);
}

bool TinyCFG::parseTaskList(TcfgTaskNode* list) {
    if (!parseTask(list, TCFG_SEQ_NONE)) {
        if (!errorRecovery) return false;
    }
    return parseOpRest(list, TCFG_SEQ_NONE);
}

const char* TinyCFG::actionName(uint8_t actionId) {
    switch (actionId) {
        case TCFG_ACT_LIGHT_ON:  return "LIGHT_ON";
        case TCFG_ACT_LIGHT_OFF: return "LIGHT_OFF";
        case TCFG_ACT_COME_HERE: return "COME_HERE";
        case TCFG_ACT_STOP:      return "STOP";
        default:                 return "UNKNOWN";
    }
}

const char* TinyCFG::seqOpName(TcfgSeqOp op) {
    switch (op) {
        case TCFG_SEQ_AND:    return "and";
        case TCFG_SEQ_THEN:   return "then";
        case TCFG_SEQ_AFTER:  return "after";
        case TCFG_SEQ_BEFORE: return "before";
        case TCFG_SEQ_IF:     return "if";
        case TCFG_SEQ_UNTIL:  return "until";
        default:              return "";
    }
}

void TinyCFG::printNode(const TcfgTaskNode* node, Stream& out, int indent) const {
    if (!node) return;
    for (int i = 0; i < indent; ++i) out.print(' ');

    if (node->type == TCFG_NODE_TASK_LIST) {
        out.println("TASK_LIST");
    } else if (node->type == TCFG_NODE_OPERATOR) {
        out.print("OP(");
        out.print(seqOpName(node->seqOp));
        out.println(")");
    } else if (node->type == TCFG_NODE_ACTION) {
        out.print(actionName(node->actionId));
        out.print("(");
        out.print(node->arg);
        out.println(")");
    }

    for (uint8_t i = 0; i < node->childCount; ++i) {
        printNode(node->children[i], out, indent + 2);
    }
}

void TinyCFG::printTaskTree(Stream& out) const {
    if (!root) {
        out.println("(empty tree)");
        return;
    }
    printNode(root, out, 0);
}

void TinyCFG::executeActions() const {
    if (!root || !actionHandler) return;

    for (uint8_t i = 0; i < root->childCount; ++i) {
        const TcfgTaskNode* child = root->children[i];
        if (child->type == TCFG_NODE_ACTION) {
            actionHandler(child->actionId, child->arg, handlerUserData);
        }
    }
}
