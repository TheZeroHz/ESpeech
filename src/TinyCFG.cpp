#include "TinyCFG.h"
#include <ctype.h>
#include <string.h>

TinyCFG::TinyCFG()
    : tokenCount(0), cursor(0), poolUsed(0), root(nullptr),
      fuzzyMatch(true), phoneticRecovery(true), errorRecovery(true),
      verbose(false),
      actionHandler(nullptr), handlerUserData(nullptr) {
    memset(&stats, 0, sizeof(stats));
    memset(&depReport, 0, sizeof(depReport));
    lastInput[0] = '\0';
    flowNormalized[0] = flowSpacedWords[0] = flowDgsNotes[0] = '\0';
    flowTokenStream[0] = flowLexicalMap[0] = flowPatternMatch[0] = '\0';
    flowTreeLine[0] = flowCdaLine[0] = flowOutput[0] = '\0';
    flowPreGlueCount = 0;
}

void TinyCFG::setVerbose(bool enable) { verbose = enable; }
bool TinyCFG::isVerbose() const { return verbose; }

void TinyCFG::setFuzzyMatch(bool enable) { fuzzyMatch = enable; }
void TinyCFG::setPhoneticRecovery(bool enable) { phoneticRecovery = enable; }
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
    memset(&depReport, 0, sizeof(depReport));
}

const TcfgTaskNode* TinyCFG::getTaskTree() const { return root; }
const TcfgParseStats& TinyCFG::getStats() const { return stats; }
const TcfgDependencyReport& TinyCFG::getDependencyReport() const { return depReport; }

uint8_t TinyCFG::getPatternCount() { return TCFG_PATTERN_COUNT; }
const char* TinyCFG::getGrammarName() { return TCFG_GRAMMAR_NAME; }
uint8_t TinyCFG::getGrammarVersion() { return TCFG_GRAMMAR_VERSION; }

bool TinyCFG::parse(const String& text) { return parse(text.c_str()); }

void TinyCFG::tokenizeOnly(const char* text) {
    reset();
    if (text && text[0]) {
        strncpy(lastInput, text, sizeof(lastInput) - 1);
        lastInput[sizeof(lastInput) - 1] = '\0';
        tokenize(text);
    }
    stats.tokensTotal = tokenCount;
    buildDataFlow();
}

bool TinyCFG::parse(const char* text) {
    reset();
    if (!text || !text[0]) return false;

    strncpy(lastInput, text, sizeof(lastInput) - 1);
    lastInput[sizeof(lastInput) - 1] = '\0';

    uint32_t t0 = micros();
    tokenize(text);
    stats.tokensTotal = tokenCount;
    stats.ramUsedBytes = sizeof(TinyCFG) + poolUsed * sizeof(TcfgTaskNode);

    if (tokenCount == 0) {
        buildDataFlow();
        return false;
    }

    root = allocNode();
    root->type = TCFG_NODE_TASK_LIST;

    bool ok = parseTaskList(root);
    stats.tokensConsumed = cursor;
    stats.parseLatencyUs = micros() - t0;

    float tokenScore = tokenCount > 0
        ? (float)(tokenCount - stats.recoveryCount - stats.noiseSkipped) / (float)tokenCount : 0.0f;
    float phoneticPenalty = stats.phoneticHits * 0.05f;
    stats.confidence = (tokenScore - phoneticPenalty) > 0.0f ? (tokenScore - phoneticPenalty) : 0.0f;

    if (ok && countActionNodes(root) > 0) {
        TinyCFGDependency::analyze(root, depReport);
    }

    buildDataFlow();

    return ok && countActionNodes(root) > 0;
}

void TinyCFG::pushToken(const char* word, uint8_t flags) {
    if (!word || !word[0] || tokenCount >= TCFG_MAX_TOKENS) return;
    tokenBuf[tokenCount] = lookupToken(word, &flags);
    tokenFlags[tokenCount] = flags;
    strncpy(tokenText[tokenCount], word, TCFG_MAX_TOKEN_TEXT - 1);
    tokenText[tokenCount][TCFG_MAX_TOKEN_TEXT - 1] = '\0';
    tokenCount++;
}

bool TinyCFG::needsGluedSegmentation(const char* text) const {
    if (!text || !text[0]) return false;
    bool hasSpace = false;
    uint8_t alnumCount = 0;
    for (const char* p = text; *p; ++p) {
        if (isspace((unsigned char)*p)) hasSpace = true;
        if (isalnum((unsigned char)*p)) alnumCount++;
    }
    return !hasSpace && alnumCount >= 5;
}

void TinyCFG::expandCamelCase(const char* in, char* out, size_t outLen) const {
    size_t o = 0;
    for (size_t i = 0; in[i] && o < outLen - 2; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (i > 0 && isupper(c)) {
            out[o++] = ' ';
        }
        out[o++] = (char)tolower(c);
    }
    out[o] = '\0';
}

void TinyCFG::tokenizeGlued(const char* text) {
    char lower[128];
    size_t len = strlen(text);
    if (len >= sizeof(lower)) len = sizeof(lower) - 1;
    for (size_t i = 0; i < len; ++i) {
        lower[i] = (char)tolower((unsigned char)text[i]);
    }
    lower[len] = '\0';

    size_t pos = 0;
    while (lower[pos] && tokenCount < TCFG_MAX_TOKENS) {
        uint8_t bestLen = 0;
        const char* bestWord = nullptr;

        for (uint8_t i = 0; i < TCFG_SYNONYM_COUNT; ++i) {
            const char* w = TCFG_SYNONYMS[i].word;
            uint8_t wlen = (uint8_t)strlen(w);
            if (wlen < 2) continue;
            if (strncmp(lower + pos, w, wlen) == 0 && wlen > bestLen) {
                bestLen = wlen;
                bestWord = w;
            }
        }

        if (bestLen > 0 && bestWord) {
            char tmp[TCFG_MAX_TOKEN_TEXT];
            strncpy(tmp, bestWord, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
            pushToken(tmp, TCFG_TOKEN_EXACT);
            stats.gluedSegments++;
            pos += bestLen;
        } else {
            pos++;
        }
    }
}

void TinyCFG::tokenizeSpaced(const char* text) {
    char word[32];
    uint8_t wi = 0;

    for (const char* p = text; *p; ++p) {
        char c = (char)tolower((unsigned char)*p);
        if (c == '\'') continue;
        if (isalnum((unsigned char)c)) {
            if (wi < sizeof(word) - 1) word[wi++] = c;
        } else if (wi > 0) {
            word[wi] = '\0';
            pushToken(word, TCFG_TOKEN_EXACT);
            wi = 0;
        }
    }
    if (wi > 0) {
        word[wi] = '\0';
        pushToken(word, TCFG_TOKEN_EXACT);
    }
    capturePreGlueTokens();
    expandInlineGluedTokens();
}

bool TinyCFG::isPureDigits(const char* word) {
    if (!word || !word[0]) return false;
    for (const char* p = word; *p; ++p) {
        if (!isdigit((unsigned char)*p)) return false;
    }
    return true;
}

bool TinyCFG::isFillerWord(const char* word) {
    if (!word) return false;
    static const char* fillers[] = {
        "this", "that", "the", "a", "an", "please", "um", "uh",
        "okay", "ok", "yeah", "just", "now", nullptr
    };
    for (uint8_t i = 0; fillers[i]; ++i) {
        if (strcmp(word, fillers[i]) == 0) return true;
    }
    return false;
}

bool TinyCFG::wordNeedsGluedSegmentation(const char* word) const {
    if (!word || !word[0]) return false;
    if (strlen(word) < 6) return false;
    if (isPureDigits(word)) return false;
    for (uint8_t i = 0; i < TCFG_SYNONYM_COUNT; ++i) {
        if (strcmp(word, TCFG_SYNONYMS[i].word) == 0) return false;
    }
    return true;
}

uint8_t TinyCFG::segmentGluedWord(const char* text, char out[][TCFG_MAX_TOKEN_TEXT],
                                  uint8_t maxOut) const {
    if (!text || maxOut == 0) return 0;

    char lower[128];
    size_t len = strlen(text);
    if (len >= sizeof(lower)) len = sizeof(lower) - 1;
    for (size_t i = 0; i < len; ++i) {
        lower[i] = (char)tolower((unsigned char)text[i]);
    }
    lower[len] = '\0';

    uint8_t n = 0;
    size_t pos = 0;
    while (lower[pos] && n < maxOut) {
        uint8_t bestLen = 0;
        const char* bestWord = nullptr;

        for (uint8_t i = 0; i < TCFG_SYNONYM_COUNT; ++i) {
            const char* w = TCFG_SYNONYMS[i].word;
            uint8_t wlen = (uint8_t)strlen(w);
            if (wlen < 2) continue;
            if (strncmp(lower + pos, w, wlen) == 0 && wlen > bestLen) {
                bestLen = wlen;
                bestWord = w;
            }
        }

        if (bestLen > 0 && bestWord) {
            strncpy(out[n], bestWord, TCFG_MAX_TOKEN_TEXT - 1);
            out[n][TCFG_MAX_TOKEN_TEXT - 1] = '\0';
            n++;
            pos += bestLen;
        } else {
            pos++;
        }
    }
    return n;
}

void TinyCFG::expandInlineGluedTokens() {
    uint8_t nBuf[TCFG_MAX_TOKENS];
    char nText[TCFG_MAX_TOKENS][TCFG_MAX_TOKEN_TEXT];
    uint8_t nFlags[TCFG_MAX_TOKENS];
    uint8_t nCount = 0;

    for (uint8_t i = 0; i < tokenCount && nCount < TCFG_MAX_TOKENS; ++i) {
        const char* w = tokenText[i];

        char expanded[160];
        expandCamelCase(w, expanded, sizeof(expanded));
        if (strchr(expanded, ' ') != nullptr && strlen(expanded) >= 5) {
            char sub[32];
            uint8_t wi = 0;
            for (const char* p = expanded; ; ++p) {
                if (*p && !isspace((unsigned char)*p)) {
                    if (wi < sizeof(sub) - 1) sub[wi++] = *p;
                } else {
                    if (wi > 0) {
                        sub[wi] = '\0';
                        uint8_t fl = TCFG_TOKEN_EXACT;
                        nBuf[nCount] = lookupToken(sub, &fl);
                        nFlags[nCount] = fl;
                        strncpy(nText[nCount], sub, TCFG_MAX_TOKEN_TEXT - 1);
                        nText[nCount][TCFG_MAX_TOKEN_TEXT - 1] = '\0';
                        nCount++;
                        stats.gluedSegments++;
                        wi = 0;
                    }
                    if (!*p) break;
                }
            }
            continue;
        }

        if (wordNeedsGluedSegmentation(w)) {
            char parts[16][TCFG_MAX_TOKEN_TEXT];
            uint8_t pn = segmentGluedWord(w, parts, 16);
            if (pn >= 2) {
                for (uint8_t j = 0; j < pn && nCount < TCFG_MAX_TOKENS; ++j) {
                    uint8_t fl = TCFG_TOKEN_EXACT;
                    nBuf[nCount] = lookupToken(parts[j], &fl);
                    nFlags[nCount] = fl;
                    strncpy(nText[nCount], parts[j], TCFG_MAX_TOKEN_TEXT - 1);
                    nText[nCount][TCFG_MAX_TOKEN_TEXT - 1] = '\0';
                    nCount++;
                    stats.gluedSegments++;
                }
                continue;
            }
        }

        if (nCount < TCFG_MAX_TOKENS) {
            if (isPureDigits(w) || isFillerWord(w)) {
                nBuf[nCount] = TCFG_TOK_UNKNOWN;
                nFlags[nCount] = TCFG_TOKEN_EXACT;
            } else {
                nBuf[nCount] = tokenBuf[i];
                nFlags[nCount] = tokenFlags[i];
            }
            strncpy(nText[nCount], w, TCFG_MAX_TOKEN_TEXT - 1);
            nText[nCount][TCFG_MAX_TOKEN_TEXT - 1] = '\0';
            nCount++;
        }
    }

    tokenCount = nCount;
    for (uint8_t i = 0; i < nCount; ++i) {
        tokenBuf[i] = nBuf[i];
        tokenFlags[i] = nFlags[i];
        strncpy(tokenText[i], nText[i], TCFG_MAX_TOKEN_TEXT);
        tokenText[i][TCFG_MAX_TOKEN_TEXT - 1] = '\0';
    }
}

void TinyCFG::tokenize(const char* text) {
    if (!text || !text[0]) return;

    if (needsGluedSegmentation(text)) {
        char expanded[160];
        expandCamelCase(text, expanded, sizeof(expanded));
        bool expandedHasSpace = false;
        for (const char* p = expanded; *p; ++p) {
            if (isspace((unsigned char)*p)) { expandedHasSpace = true; break; }
        }
        if (expandedHasSpace) {
            tokenizeSpaced(expanded);
            if (tokenCount > 1) return;
            tokenCount = 0;
        }
        tokenizeGlued(text);
        return;
    }

    tokenizeSpaced(text);
}

uint8_t TinyCFG::lookupToken(const char* word, uint8_t* flagsOut) {
    if (isPureDigits(word) || isFillerWord(word)) {
        if (flagsOut) *flagsOut = TCFG_TOKEN_EXACT;
        return TCFG_TOK_UNKNOWN;
    }
    for (uint8_t i = 0; i < TCFG_SYNONYM_COUNT; ++i) {
        if (strcmp(word, TCFG_SYNONYMS[i].word) == 0) {
            if (flagsOut) *flagsOut = TCFG_TOKEN_EXACT;
            return TCFG_SYNONYMS[i].terminal;
        }
    }
    if (phoneticRecovery) {
        uint8_t p = phoneticLookup(word);
        if (p != TCFG_TOK_UNKNOWN) {
            if (flagsOut) *flagsOut = TCFG_TOKEN_PHON;
            return p;
        }
    }
    if (fuzzyMatch) {
        uint8_t f = editDistanceLookup(word);
        if (f != TCFG_TOK_UNKNOWN) {
            if (flagsOut) *flagsOut = TCFG_TOKEN_FUZZY;
            return f;
        }
    }
    if (flagsOut) *flagsOut = TCFG_TOKEN_EXACT;
    return TCFG_TOK_UNKNOWN;
}

uint8_t TinyCFG::phoneticLookup(const char* word) {
    char key[5] = {0};
    if (!word || !word[0]) return TCFG_TOK_UNKNOWN;

    char upper[32];
    strncpy(upper, word, sizeof(upper) - 1);
    for (char* p = upper; *p; ++p) *p = (char)toupper((unsigned char)*p);

    char first = upper[0];
    const char* map[] = {"BFPV1", "CGJKQSXZ2", "DT3", "L4", "MN5", "R6"};
    key[0] = first;
    uint8_t ki = 1;
    for (const char* p = upper + 1; *p && ki < 4; ++p) {
        char digit = '0';
        for (uint8_t m = 0; m < 6; ++m) {
            if (strchr(map[m], *p)) {
                digit = map[m][strlen(map[m]) - 1];
                break;
            }
        }
        if (digit != '0' && key[ki - 1] != digit) key[ki++] = digit;
    }
    while (ki < 4) key[ki++] = '0';

    for (uint8_t i = 0; i < TCFG_SYNONYM_COUNT; ++i) {
        if (strncmp(key, TCFG_SYNONYMS[i].phonetic, 4) == 0) {
            stats.phoneticHits++;
            return TCFG_SYNONYMS[i].terminal;
        }
    }
    return TCFG_TOK_UNKNOWN;
}

uint8_t TinyCFG::editDistanceLookup(const char* word) const {
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

uint8_t TinyCFG::peek(uint8_t offset) const {
    uint8_t pos = cursor + offset;
    if (pos >= tokenCount) return TCFG_TOK_EOF;
    return tokenBuf[pos];
}

bool TinyCFG::atEnd() const { return cursor >= tokenCount; }

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

bool TinyCFG::isOpToken(uint8_t tok) const {
    for (uint8_t i = 0; i < TCFG_OP_TOKEN_COUNT; ++i) {
        if (TCFG_OP_TOKENS[i] == tok) return true;
    }
    return false;
}

TcfgSeqOp TinyCFG::peekOp() const {
    uint8_t p = peek();
    if (p == TCFG_TOK_AND)    return TCFG_SEQ_AND;
    if (p == TCFG_TOK_OR)     return TCFG_SEQ_OR;
    if (p == TCFG_TOK_THEN)   return TCFG_SEQ_THEN;
    if (p == TCFG_TOK_AFTER)  return TCFG_SEQ_AFTER;
    if (p == TCFG_TOK_BEFORE) return TCFG_SEQ_BEFORE;
    if (p == TCFG_TOK_IF)     return TCFG_SEQ_IF;
    if (p == TCFG_TOK_UNTIL)  return TCFG_SEQ_UNTIL;
    if (p == TCFG_TOK_WHILE)  return TCFG_SEQ_WHILE;
    return TCFG_SEQ_NONE;
}

TcfgSeqOp TinyCFG::consumeOp() {
    TcfgSeqOp op = peekOp();
    if (op != TCFG_SEQ_NONE) cursor++;
    return op;
}

bool TinyCFG::isLocationToken(uint8_t tok) const {
    for (uint8_t i = 0; i < TCFG_LOCATION_COUNT; ++i) {
        if (TCFG_LOCATION_TOKENS[i] == tok) return true;
    }
    return false;
}

bool TinyCFG::isDeviceToken(uint8_t tok) const {
    for (uint8_t i = 0; i < TCFG_DEVICE_COUNT; ++i) {
        if (TCFG_DEVICE_TOKENS[i] == tok) return true;
    }
    return false;
}

bool TinyCFG::isDigitNoise(uint8_t idx) const {
    if (idx >= tokenCount) return false;
    const char* w = tokenText[idx];
    if (!w[0]) return true;
    for (const char* p = w; *p; ++p) {
        if (!isdigit((unsigned char)*p)) return false;
    }
    return true;
}

bool TinyCFG::isSkippableNoise(uint8_t idx) const {
    if (idx >= tokenCount) return false;
    if (isDigitNoise(idx)) return true;
    if (isFillerWord(tokenText[idx])) return true;
    if (tokenBuf[idx] == TCFG_TOK_UNKNOWN) return true;
    return false;
}

bool TinyCFG::shouldSkipForPattern(uint8_t idx, uint8_t patTok, uint8_t slotType) const {
    if (tokenMatchesPattern(patTok, slotType, tokenBuf[idx], idx)) return false;
    if (isDigitNoise(idx)) return true;
    if (tokenBuf[idx] == TCFG_TOK_UNKNOWN) return true;
    if (tokenFlags[idx] == TCFG_TOKEN_FUZZY || tokenFlags[idx] == TCFG_TOKEN_PHON) return true;
    return false;
}

bool TinyCFG::isValidNameToken(uint8_t tok, uint8_t idx) const {
    if (tok != TCFG_TOK_UNKNOWN) return false;
    if (isDigitNoise(idx)) return false;
    return tokenText[idx][0] != '\0';
}

bool TinyCFG::tokenMatchesPattern(uint8_t patTok, uint8_t slotType,
                                  uint8_t inputTok, uint8_t inputIdx) const {
    if (patTok == TCFG_SLOT_LOCATION || slotType == TCFG_SLOT_LOC) {
        return isLocationToken(inputTok);
    }
    if (patTok == TCFG_SLOT_NAME || slotType == TCFG_SLOT_NM) {
        return isValidNameToken(inputTok, inputIdx);
    }
    if (patTok == TCFG_SLOT_DEVICE || slotType == TCFG_SLOT_DEV) {
        return isDeviceToken(inputTok);
    }
    return patTok == inputTok;
}

bool TinyCFG::tryMatchPatternAt(uint8_t patIdx, uint8_t startPos, uint8_t& endPos,
                                char* capLoc, char* capName, char* capDev,
                                uint8_t& skips) const {
    const TcfgPattern& pat = TCFG_PATTERNS[patIdx];
    uint8_t inputIdx = startPos;
    skips = 0;
    capLoc[0] = capName[0] = capDev[0] = '\0';

    for (uint8_t ti = 0; ti < pat.token_count; ++ti) {
        uint8_t patTok = pat.tokens[ti];
        uint8_t slotType = pat.slot_types ? pat.slot_types[ti] : TCFG_SLOT_LIT;
        bool found = false;
        uint8_t localSkips = 0;

        while (inputIdx < tokenCount && localSkips <= TCFG_MAX_NOISE_SKIP) {
            if (shouldSkipForPattern(inputIdx, patTok, slotType) &&
                !(patTok == TCFG_SLOT_NAME || slotType == TCFG_SLOT_NM)) {
                inputIdx++;
                localSkips++;
                continue;
            }

            if (tokenMatchesPattern(patTok, slotType, tokenBuf[inputIdx], inputIdx)) {
                if (patTok == TCFG_SLOT_LOCATION || slotType == TCFG_SLOT_LOC) {
                    strncpy(capLoc, tokenText[inputIdx], TCFG_MAX_TOKEN_TEXT - 1);
                } else if (patTok == TCFG_SLOT_NAME || slotType == TCFG_SLOT_NM) {
                    strncpy(capName, tokenText[inputIdx], TCFG_MAX_TOKEN_TEXT - 1);
                } else if (patTok == TCFG_SLOT_DEVICE || slotType == TCFG_SLOT_DEV) {
                    strncpy(capDev, tokenText[inputIdx], TCFG_MAX_TOKEN_TEXT - 1);
                }
                inputIdx++;
                found = true;
                break;
            }

            if (patTok == TCFG_SLOT_NAME || slotType == TCFG_SLOT_NM ||
                patTok == TCFG_SLOT_LOCATION || slotType == TCFG_SLOT_LOC ||
                patTok == TCFG_SLOT_DEVICE || slotType == TCFG_SLOT_DEV) {
                break;
            }

            if (shouldSkipForPattern(inputIdx, patTok, slotType)) {
                inputIdx++;
                localSkips++;
                continue;
            }
            break;
        }

        if (!found) return false;
        skips += localSkips;
    }

    endPos = inputIdx;
    return true;
}

bool TinyCFG::matchPattern(uint8_t& actionId, char* argOut, uint8_t& domain, uint8_t& resource) {
    uint8_t bestPat = 255;
    uint8_t bestEnd = cursor;
    uint8_t bestSpan = 0;
    uint8_t bestSkips = 255;
    char bestLoc[TCFG_MAX_TOKEN_TEXT] = {0};
    char bestName[TCFG_MAX_TOKEN_TEXT] = {0};
    char bestDev[TCFG_MAX_TOKEN_TEXT] = {0};

    for (uint8_t pi = 0; pi < TCFG_PATTERN_COUNT; ++pi) {
        char capLoc[TCFG_MAX_TOKEN_TEXT] = {0};
        char capName[TCFG_MAX_TOKEN_TEXT] = {0};
        char capDev[TCFG_MAX_TOKEN_TEXT] = {0};
        uint8_t endPos = cursor;
        uint8_t skips = 0;

        if (!tryMatchPatternAt(pi, cursor, endPos, capLoc, capName, capDev, skips)) {
            continue;
        }

        uint8_t span = endPos - cursor;
        bool better = (span > bestSpan) ||
                      (span == bestSpan && skips < bestSkips) ||
                      (bestPat == 255);

        if (better) {
            bestPat = pi;
            bestEnd = endPos;
            bestSpan = span;
            bestSkips = skips;
            strncpy(bestLoc, capLoc, TCFG_MAX_TOKEN_TEXT - 1);
            strncpy(bestName, capName, TCFG_MAX_TOKEN_TEXT - 1);
            strncpy(bestDev, capDev, TCFG_MAX_TOKEN_TEXT - 1);
        }
    }

    if (bestPat == 255) return false;

    const TcfgPattern& win = TCFG_PATTERNS[bestPat];
    cursor = bestEnd;
    actionId = win.action;
    domain = win.domain;
    resource = win.resource;

    if (bestSkips > 0) {
        stats.noiseSkipped += bestSkips;
        stats.recoveryCount += bestSkips;
    }

    if (win.arg_template && (strstr(win.arg_template, "{") != nullptr)) {
        buildArgFromTemplate(win.arg_template, bestLoc, bestName, bestDev, argOut);
    } else if (win.arg_template) {
        strncpy(argOut, win.arg_template, TCFG_MAX_ARG_LEN - 1);
    } else {
        argOut[0] = '\0';
    }

    stats.patternMatches++;
    return true;
}

static void replaceToken(char* buf, size_t bufLen, const char* key, const char* val) {
    if (!val || !val[0]) {
        char empty[] = "";
        val = empty;
    }
    char tmp[TCFG_MAX_ARG_LEN];
    strncpy(tmp, buf, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char* pos = strstr(tmp, key);
    if (!pos) return;
    char out[TCFG_MAX_ARG_LEN];
    size_t prefix = (size_t)(pos - tmp);
    if (prefix >= sizeof(out)) return;
    strncpy(out, tmp, prefix);
    out[prefix] = '\0';
    strncat(out, val, sizeof(out) - strlen(out) - 1);
    strncat(out, pos + strlen(key), sizeof(out) - strlen(out) - 1);
    strncpy(buf, out, bufLen - 1);
    buf[bufLen - 1] = '\0';
}

void TinyCFG::buildArgFromTemplate(const char* tmpl, const char* loc,
                                   const char* name, const char* device,
                                   char* out) const {
    if (!tmpl || !tmpl[0]) {
        out[0] = '\0';
        return;
    }
    strncpy(out, tmpl, TCFG_MAX_ARG_LEN - 1);
    out[TCFG_MAX_ARG_LEN - 1] = '\0';
    replaceToken(out, TCFG_MAX_ARG_LEN, "{location}", loc);
    replaceToken(out, TCFG_MAX_ARG_LEN, "{name}", name);
    replaceToken(out, TCFG_MAX_ARG_LEN, "{device}", device);
}

void TinyCFG::skipNoise() {
    while (cursor < tokenCount && isSkippableNoise(cursor)) {
        cursor++;
        stats.recoveryCount++;
        stats.noiseSkipped++;
    }
}

bool TinyCFG::parseSingleTask(TcfgTaskNode* list, TcfgSeqOp op) {
    if (atEnd() || isOpToken(peek())) return false;

    uint8_t actionId = 0;
    char arg[TCFG_MAX_ARG_LEN] = {0};
    uint8_t domain = 0;
    uint8_t resource = 0;

    for (uint8_t start = cursor; start < tokenCount; ++start) {
        if (start != cursor && isSkippableNoise(start)) continue;

        uint8_t saved = cursor;
        cursor = start;

        if (matchPattern(actionId, arg, domain, resource)) {
            TcfgTaskNode* action = allocNode();
            if (!action) { cursor = saved; return false; }
            action->type = TCFG_NODE_ACTION;
            action->actionId = actionId;
            action->domain = domain;
            action->resource = resource;
            action->seqOp = op;
            strncpy(action->arg, arg, TCFG_MAX_ARG_LEN - 1);
            attachChild(list, action);
            return true;
        }
        cursor = saved;
    }

    if (errorRecovery) {
        stats.recoveryCount++;
        skipNoise();
    }
    return false;
}

uint8_t TinyCFG::countActionNodes(const TcfgTaskNode* list) const {
    if (!list) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < list->childCount; ++i) {
        if (list->children[i]->type == TCFG_NODE_ACTION) n++;
    }
    return n;
}

bool TinyCFG::canStartCommandAt(uint8_t startPos) const {
    if (startPos >= tokenCount) return false;
    char capLoc[TCFG_MAX_TOKEN_TEXT];
    char capName[TCFG_MAX_TOKEN_TEXT];
    char capDev[TCFG_MAX_TOKEN_TEXT];
    for (uint8_t pi = 0; pi < TCFG_PATTERN_COUNT; ++pi) {
        uint8_t endPos = startPos;
        uint8_t skips = 0;
        if (tryMatchPatternAt(pi, startPos, endPos, capLoc, capName, capDev, skips)) {
            return true;
        }
    }
    return false;
}

bool TinyCFG::parseOpRest(TcfgTaskNode* list) {
    TcfgSeqOp op = peekOp();
    bool explicitOp = (op != TCFG_SEQ_NONE);

    if (!explicitOp) {
        skipNoise();
        if (atEnd() || !canStartCommandAt(cursor)) {
            return true;
        }
        op = TCFG_SEQ_JUXTA;
    } else {
        consumeOp();
        skipNoise();
        while (peekOp() != TCFG_SEQ_NONE) {
            consumeOp();
            skipNoise();
        }
    }

    if (!parseSingleTask(list, op)) {
        return explicitOp ? parseOpRest(list) : true;
    }

    TcfgTaskNode* action = list->children[list->childCount - 1];
    list->childCount--;

    TcfgTaskNode* opNode = allocNode();
    if (opNode) {
        opNode->type = TCFG_NODE_OPERATOR;
        opNode->seqOp = op;
        attachChild(list, opNode);
    }
    attachChild(list, action);

    return parseOpRest(list);
}

bool TinyCFG::parseTaskList(TcfgTaskNode* list) {
    skipNoise();
    if (!parseSingleTask(list, TCFG_SEQ_NONE)) {
        return false;
    }
    return parseOpRest(list);
}

const char* TinyCFG::actionName(uint8_t actionId) {
    if (actionId < TCFG_ACTION_COUNT + 1) {
        return TCFG_ACTION_NAMES[actionId];
    }
    return "UNKNOWN";
}

const char* TinyCFG::domainName(uint8_t domainId) {
    if (domainId < 7) return TCFG_DOMAIN_NAMES[domainId];
    return "unknown";
}

const char* TinyCFG::seqOpName(TcfgSeqOp op) {
    switch (op) {
        case TCFG_SEQ_AND:    return "and";
        case TCFG_SEQ_OR:     return "or";
        case TCFG_SEQ_JUXTA:  return "(back-to-back)";
        case TCFG_SEQ_THEN:   return "then";
        case TCFG_SEQ_AFTER:  return "after";
        case TCFG_SEQ_BEFORE: return "before";
        case TCFG_SEQ_IF:     return "if";
        case TCFG_SEQ_UNTIL:  return "until";
        case TCFG_SEQ_WHILE:  return "while";
        default:              return "";
    }
}

void TinyCFG::printTokens(Stream& out) const {
    out.print("Tokens: [");
    for (uint8_t i = 0; i < tokenCount; ++i) {
        if (i > 0) out.print(" | ");
        out.printf("%s(%u)", tokenText[i], tokenBuf[i]);
    }
    out.println("]");
}

void TinyCFG::printNode(const TcfgTaskNode* node, Stream& out, int indent) const {
    if (!node) return;
    for (int i = 0; i < indent; ++i) out.print(' ');

    if (node->type == TCFG_NODE_TASK_LIST) {
        out.println("TASK_LIST");
    } else if (node->type == TCFG_NODE_OPERATOR) {
        out.printf("OP(%s)\n", seqOpName(node->seqOp));
    } else if (node->type == TCFG_NODE_ACTION) {
        out.printf("%s(%s) [%s]\n",
            actionName(node->actionId), node->arg, domainName(node->domain));
    }

    for (uint8_t i = 0; i < node->childCount; ++i) {
        printNode(node->children[i], out, indent + 2);
    }
}

void TinyCFG::printTaskTree(Stream& out) const {
    if (!root) { out.println("(empty tree)"); return; }
    out.println("--- Hierarchical Task Tree ---");
    printNode(root, out, 0);
}

void TinyCFG::printDependencyReport(Stream& out) const {
    TinyCFGDependency::printReport(depReport, out);
}

void TinyCFG::printMetrics(Stream& out) const {
    out.println("--- TinyCFG Parse Metrics ---");
    out.printf("Grammar: %s v%u\n", getGrammarName(), getGrammarVersion());
    out.printf("Patterns compiled: %u\n", getPatternCount());
    out.printf("Latency: %u us\n", stats.parseLatencyUs);
    out.printf("RAM estimate: %u bytes\n", stats.ramUsedBytes);
    out.printf("Tokens: %u/%u consumed\n", stats.tokensConsumed, stats.tokensTotal);
    out.printf("Pattern matches: %u\n", stats.patternMatches);
    out.printf("Phonetic recoveries: %u\n", stats.phoneticHits);
    out.printf("Glued-word segments: %u\n", stats.gluedSegments);
    out.printf("Noise tokens skipped: %u\n", stats.noiseSkipped);
    out.printf("Error recoveries: %u\n", stats.recoveryCount);
    out.printf("Confidence: %.1f%%\n", stats.confidence * 100.0f);
}

static const char* tokenFlagLabel(uint8_t flag) {
    switch (flag) {
        case TCFG_TOKEN_FUZZY: return "FUZZY";
        case TCFG_TOKEN_PHON:  return "PHONETIC";
        default:               return "EXACT";
    }
}

void TinyCFG::printRecoveryConfig(Stream& out) const {
    out.println("  Recovery settings:");
    out.printf("    Fuzzy match (edit-distance): %s\n", fuzzyMatch ? "ON" : "OFF");
    out.printf("    PER (phonetic):              %s\n", phoneticRecovery ? "ON" : "OFF");
    out.printf("    Error recovery (gap/noise):  %s\n", errorRecovery ? "ON" : "OFF");
    out.printf("    Max noise skip per gap:      %u tokens\n", (unsigned)TCFG_MAX_NOISE_SKIP);
}

void TinyCFG::printLexicalDetail(Stream& out) const {
    out.println("  Token stream after tokenization:");
    if (tokenCount == 0) {
        out.println("    (no tokens)");
        return;
    }
    for (uint8_t i = 0; i < tokenCount; ++i) {
        const char* role = "command-word";
        if (isPureDigits(tokenText[i])) role = "noise-digit";
        else if (isFillerWord(tokenText[i])) role = "noise-filler";
        else if (tokenBuf[i] == TCFG_TOK_UNKNOWN) role = "unknown/name-slot";
        else if (isLocationToken(tokenBuf[i])) role = "location";
        else if (isOpToken(tokenBuf[i])) role = "operator";
        else if (isDeviceToken(tokenBuf[i])) role = "device";

        out.printf("    [%02u] \"%-14s\" id=%-3u  %-8s  (%s)\n",
            i, tokenText[i], tokenBuf[i], tokenFlagLabel(tokenFlags[i]), role);
    }
    if (stats.gluedSegments > 0) {
        out.printf("  DGS: %u sub-words produced from glued/camelCase input\n",
            stats.gluedSegments);
    }
}

void TinyCFG::printActionsSummary(Stream& out) const {
    out.println("  Matched semantic actions:");
    if (!root || countActionNodes(root) == 0) {
        out.println("    (none — no pattern matched)");
        return;
    }
    uint8_t actionNum = 0;
    for (uint8_t i = 0; i < root->childCount; ++i) {
        const TcfgTaskNode* child = root->children[i];
        if (child->type == TCFG_NODE_OPERATOR) {
            const char* hint = "";
            switch (child->seqOp) {
                case TCFG_SEQ_AND:   hint = " — execute both"; break;
                case TCFG_SEQ_OR:    hint = " — pick one (exclusive)"; break;
                case TCFG_SEQ_JUXTA: hint = " — implicit sequence (no connector)"; break;
                default: break;
            }
            out.printf("    --> operator: %s%s\n", seqOpName(child->seqOp), hint);
        } else if (child->type == TCFG_NODE_ACTION) {
            actionNum++;
            out.printf("    [%u] %s(%s)  domain=%s  resource=%u",
                actionNum,
                actionName(child->actionId),
                child->arg[0] ? child->arg : "",
                domainName(child->domain),
                child->resource);
            if (child->seqOp != TCFG_SEQ_NONE) {
                out.printf("  via-op=%s", seqOpName(child->seqOp));
            }
            out.println();
        }
    }
    out.printf("  Total pattern matches: %u\n", stats.patternMatches);
}

// --- Data-flow snapshot helpers ---

void TinyCFG::strAppend(char* buf, size_t bufLen, const char* piece) {
    if (!buf || !piece || bufLen == 0) return;
    size_t len = strlen(buf);
    size_t avail = bufLen - len - 1;
    if (avail == 0) return;
    strncat(buf, piece, avail);
}

void TinyCFG::capturePreGlueTokens() {
    flowPreGlueCount = tokenCount;
    for (uint8_t i = 0; i < tokenCount; ++i) {
        strncpy(flowPreGlue[i], tokenText[i], TCFG_MAX_TOKEN_TEXT - 1);
        flowPreGlue[i][TCFG_MAX_TOKEN_TEXT - 1] = '\0';
    }
}

void TinyCFG::buildNormalizedPreview(const char* text, char* out, size_t outLen) const {
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!text) return;
    size_t o = 0;
    for (const char* p = text; *p && o < outLen - 1; ++p) {
        char c = (char)tolower((unsigned char)*p);
        if (c == '\'') continue;
        if (isalnum((unsigned char)c)) {
            out[o++] = c;
        } else if (o > 0 && out[o - 1] != ' ') {
            out[o++] = ' ';
        }
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = '\0';
}

void TinyCFG::joinWords(const char words[][TCFG_MAX_TOKEN_TEXT], uint8_t count,
                        char* out, size_t outLen) const {
    if (!out || outLen == 0) return;
    out[0] = '\0';
    for (uint8_t i = 0; i < count; ++i) {
        char piece[TCFG_MAX_TOKEN_TEXT + 4];
        snprintf(piece, sizeof(piece), "%s[%s]", i > 0 ? " " : "", words[i]);
        strAppend(out, outLen, piece);
    }
}

const char* TinyCFG::terminalLabel(uint8_t tok) const {
    if (tok == TCFG_TOK_UNKNOWN) return "UNKNOWN";
    if (tok == TCFG_TOK_EOF) return "EOF";
    for (uint8_t i = 0; i < TCFG_SYNONYM_COUNT; ++i) {
        if (TCFG_SYNONYMS[i].terminal == tok) {
            return TCFG_SYNONYMS[i].word;
        }
    }
    return "?";
}

void TinyCFG::buildDataFlow() {
    flowNormalized[0] = flowSpacedWords[0] = flowDgsNotes[0] = '\0';
    flowTokenStream[0] = flowLexicalMap[0] = flowPatternMatch[0] = '\0';
    flowTreeLine[0] = flowCdaLine[0] = flowOutput[0] = '\0';

    if (!lastInput[0]) return;

    buildNormalizedPreview(lastInput, flowNormalized, sizeof(flowNormalized));

    if (flowPreGlueCount == 0) {
        char single[TCFG_MAX_TOKEN_TEXT];
        buildNormalizedPreview(lastInput, single, sizeof(single));
        bool hasSpace = false;
        for (const char* p = single; *p; ++p) {
            if (isspace((unsigned char)*p)) { hasSpace = true; break; }
        }
        if (!hasSpace && single[0]) {
            strncpy(flowPreGlue[0], single, TCFG_MAX_TOKEN_TEXT - 1);
            flowPreGlue[0][TCFG_MAX_TOKEN_TEXT - 1] = '\0';
            flowPreGlueCount = 1;
        }
    }

    joinWords(flowPreGlue, flowPreGlueCount, flowSpacedWords, sizeof(flowSpacedWords));

    for (uint8_t i = 0; i < flowPreGlueCount; ++i) {
        const char* w = flowPreGlue[i];
        bool foundWhole = false;
        for (uint8_t j = 0; j < tokenCount; ++j) {
            if (strcmp(tokenText[j], w) == 0) { foundWhole = true; break; }
        }
        if (foundWhole) continue;

        char expanded[160];
        expandCamelCase(w, expanded, sizeof(expanded));
        if (strchr(expanded, ' ') != nullptr && strlen(expanded) >= 5) {
            char note[64];
            snprintf(note, sizeof(note), "%s%s -> %s",
                flowDgsNotes[0] ? "; " : "", w, expanded);
            strAppend(flowDgsNotes, sizeof(flowDgsNotes), note);
            continue;
        }

        char parts[16][TCFG_MAX_TOKEN_TEXT];
        uint8_t pn = segmentGluedWord(w, parts, 16);
        if (pn >= 2) {
            char note[80];
            snprintf(note, sizeof(note), "%s%s -> ", flowDgsNotes[0] ? "; " : "", w);
            strAppend(flowDgsNotes, sizeof(flowDgsNotes), note);
            for (uint8_t j = 0; j < pn; ++j) {
                char piece[TCFG_MAX_TOKEN_TEXT + 4];
                snprintf(piece, sizeof(piece), "%s%s", j > 0 ? "+" : "", parts[j]);
                strAppend(flowDgsNotes, sizeof(flowDgsNotes), piece);
            }
        }
    }
    if (!flowDgsNotes[0] && stats.gluedSegments > 0) {
        snprintf(flowDgsNotes, sizeof(flowDgsNotes),
            "(%u glued segment(s) expanded)", stats.gluedSegments);
    }

    joinWords(tokenText, tokenCount, flowTokenStream, sizeof(flowTokenStream));

    for (uint8_t i = 0; i < tokenCount; ++i) {
        char line[72];
        const char* word = tokenText[i];
        uint8_t tok = tokenBuf[i];
        const char* via = tokenFlagLabel(tokenFlags[i]);

        if (isPureDigits(word)) {
            snprintf(line, sizeof(line), "%s\"%s\" -> [SKIP digit-noise]",
                flowLexicalMap[0] ? "\n" : "", word);
        } else if (isFillerWord(word)) {
            snprintf(line, sizeof(line), "%s\"%s\" -> [SKIP filler]",
                flowLexicalMap[0] ? "\n" : "", word);
        } else if (tok == TCFG_TOK_UNKNOWN) {
            snprintf(line, sizeof(line), "%s\"%s\" -> [SLOT/name] id=UNKNOWN",
                flowLexicalMap[0] ? "\n" : "", word);
        } else if (tokenFlags[i] == TCFG_TOKEN_EXACT &&
                   strcmp(word, terminalLabel(tok)) == 0) {
            snprintf(line, sizeof(line), "%s\"%s\" -> %s(id=%u) [EXACT]",
                flowLexicalMap[0] ? "\n" : "", word, terminalLabel(tok), tok);
        } else {
            snprintf(line, sizeof(line), "%s\"%s\" --[%s]--> %s(id=%u)",
                flowLexicalMap[0] ? "\n" : "", word, via, terminalLabel(tok), tok);
        }
        strAppend(flowLexicalMap, sizeof(flowLexicalMap), line);
    }

    if (!root || countActionNodes(root) == 0) {
        strncpy(flowPatternMatch, "(no pattern matched)", sizeof(flowPatternMatch) - 1);
        strncpy(flowTreeLine, "(empty tree)", sizeof(flowTreeLine) - 1);
        strncpy(flowOutput, "(nothing to dispatch)", sizeof(flowOutput) - 1);
    } else {
        uint8_t actionNum = 0;
        for (uint8_t i = 0; i < root->childCount; ++i) {
            const TcfgTaskNode* child = root->children[i];
            if (child->type == TCFG_NODE_OPERATOR) {
                char piece[24];
                snprintf(piece, sizeof(piece), " %s ", seqOpName(child->seqOp));
                strAppend(flowPatternMatch, sizeof(flowPatternMatch), piece);
                strAppend(flowTreeLine, sizeof(flowTreeLine), piece);
            } else if (child->type == TCFG_NODE_ACTION) {
                actionNum++;
                char piece[64];
                snprintf(piece, sizeof(piece), "%s%s(%s)",
                    flowPatternMatch[0] ? "" : "",
                    actionName(child->actionId),
                    child->arg[0] ? child->arg : "-");
                strAppend(flowPatternMatch, sizeof(flowPatternMatch), piece);

                char treePiece[72];
                snprintf(treePiece, sizeof(treePiece), "%s%s(%s)",
                    flowTreeLine[0] ? "" : "TASK_LIST: ",
                    actionName(child->actionId),
                    child->arg[0] ? child->arg : "-");
                strAppend(flowTreeLine, sizeof(flowTreeLine), treePiece);

                char outPiece[72];
                snprintf(outPiece, sizeof(outPiece), "%sEXEC %s(%s)",
                    flowOutput[0] ? ", " : "",
                    actionName(child->actionId),
                    child->arg[0] ? child->arg : "");
                strAppend(flowOutput, sizeof(flowOutput), outPiece);
            }
        }
    }

    snprintf(flowCdaLine, sizeof(flowCdaLine),
        "tasks=%u deps=%u conflicts=%u safe=%s",
        depReport.taskCount, depReport.depCount, depReport.conflictCount,
        depReport.executionSafe ? "YES" : "NO");
}

const char* TinyCFG::getDataFlowStage(uint8_t stage) const {
    switch (stage) {
        case 0:  return lastInput;
        case 1:  return flowNormalized;
        case 2:  return flowSpacedWords;
        case 3:  return flowDgsNotes[0] ? flowDgsNotes : "(no deglue)";
        case 4:  return flowTokenStream;
        case 5:  return flowLexicalMap;
        case 6:  return flowPatternMatch;
        case 7:  return flowTreeLine;
        case 8:  return flowCdaLine;
        case 9:  return flowOutput;
        default: return "";
    }
}

void TinyCFG::printDataFlow(Stream& out) const {
    out.println();
    out.println("=================================================");
    out.println("  DATA FLOW (input -> output transformations)");
    out.println("=================================================");
    out.println();
    out.println("[0] RAW INPUT");
    out.printf("    \"%s\"\n", lastInput[0] ? lastInput : "(empty)");
    out.println("         |");
    out.println("         v  lowercase, strip punctuation/apostrophes");
    out.println("[1] NORMALIZED TEXT");
    out.printf("    \"%s\"\n", flowNormalized[0] ? flowNormalized : "(empty)");
    out.println("         |");
    out.println("         v  split on spaces / punctuation");
    out.println("[2] SPACED WORDS (pre-DGS)");
    out.printf("    %s\n", flowSpacedWords[0] ? flowSpacedWords : "(none)");
    out.println("         |");
    out.println("         v  DGS: camelCase split + dictionary deglue");
    out.println("[3] DGS TRANSFORMS");
    out.printf("    %s\n", flowDgsNotes[0] ? flowDgsNotes : "(unchanged)");
    out.println("         |");
    out.println("         v  token stream after expansion");
    out.println("[4] TOKEN STREAM");
    out.printf("    %s\n", flowTokenStream[0] ? flowTokenStream : "(none)");
    out.println("         |");
    out.println("         v  lexical lookup: exact -> PER -> fuzzy; skip digits/fillers");
    out.println("[5] LEXICAL MAPPING");
    if (flowLexicalMap[0]) {
        out.print("   ");
        out.println(flowLexicalMap);
    } else {
        out.println("    (no tokens)");
    }
    out.println("         |");
    out.println("         v  pattern match (gap-tolerant) + slot fill");
    out.println("[6] SEMANTIC MATCHES");
    out.printf("    %s\n", flowPatternMatch[0] ? flowPatternMatch : "(none)");
    out.println("         |");
    out.println("         v  hierarchical task tree + operators");
    out.println("[7] TASK TREE");
    out.printf("    %s\n", flowTreeLine[0] ? flowTreeLine : "(empty)");
    out.println("         |");
    out.println("         v  CDA dependency / conflict check");
    out.println("[8] CDA RESULT");
    out.printf("    %s\n", flowCdaLine[0] ? flowCdaLine : "(skipped)");
    out.println("         |");
    out.println("         v  semantic action dispatch");
    out.println("[9] OUTPUT / DISPATCH");
    out.printf("    %s\n", flowOutput[0] ? flowOutput : "(none)");
    out.println();
}

void TinyCFG::printPipeline(Stream& out) const {
    printDataFlow(out);

    out.println("=================================================");
    out.println("  TinyCFG PIPELINE TRACE (step-by-step)");
    out.println("=================================================");

    out.println();
    out.println("[STEP 1] RAW INPUT");
    out.printf("  Text: \"%s\"\n", lastInput[0] ? lastInput : "(empty)");
    out.printf("  Grammar: %s v%u | %u compiled patterns\n",
        getGrammarName(), getGrammarVersion(), getPatternCount());

    out.println();
    out.println("[STEP 2] TOKENIZATION");
    out.println("  Split on spaces/punctuation, lowercase, strip apostrophes.");
    out.println("  DGS: deglue camelCase & dictionary-split (turnonfan->turn+on+fan).");
    printLexicalDetail(out);

    out.println();
    out.println("[STEP 3] LEXICAL LOOKUP");
    out.println("  Map words -> terminals: exact synonym -> PER -> fuzzy.");
    out.println("  Skip: pure digits (120), filler words (this, the, please).");
    printRecoveryConfig(out);
  if (stats.phoneticHits > 0) {
        out.printf("  PER hits this parse: %u\n", stats.phoneticHits);
    }

    out.println();
    out.println("[STEP 4] PATTERN MATCHING (LL(1) + gap-tolerant)");
    out.println("  Match token stream against compiled [patterns] from .cfg.");
    out.println("  Longest pattern wins; noise tokens skipped between literals.");
    out.println("  Slots: {location}=zone, {name}=user label, {device}=device type.");
    printActionsSummary(out);
    if (stats.noiseSkipped > 0) {
        out.printf("  Noise tokens skipped during match: %u\n", stats.noiseSkipped);
    }

    out.println();
    out.println("[STEP 5] HIERARCHICAL TASK TREE");
    if (root && countActionNodes(root) > 0) {
        printNode(root, out, 2);
    } else {
        out.println("  (empty — parse did not produce actions)");
    }

    out.println();
    out.println("[STEP 6] COMMAND DEPENDENCY ANALYSIS (CDA)");
    if (countActionNodes(root) > 0) {
        printDependencyReport(out);
    } else {
        out.println("  (skipped — no actions to analyze)");
    }

    out.println();
    out.println("[STEP 7] PARSE METRICS");
    out.printf("  Latency:     %u us\n", stats.parseLatencyUs);
    out.printf("  RAM:         %u bytes\n", stats.ramUsedBytes);
    out.printf("  Tokens:      %u/%u consumed\n", stats.tokensConsumed, stats.tokensTotal);
    out.printf("  Confidence:  %.1f%%\n", stats.confidence * 100.0f);
    out.printf("  Recoveries:  %u\n", stats.recoveryCount);

    out.println();
    out.println("[STEP 8] SEMANTIC DISPATCH");
    if (countActionNodes(root) > 0 && actionHandler) {
        out.println("  and=both | or=first alternative | back-to-back=sequential");
        dispatchActions(out);
    } else if (countActionNodes(root) > 0) {
        out.println("  (no handler registered — use setActionHandler())");
    } else {
        out.println("  (nothing to dispatch)");
    }

    out.println();
    out.println("=================================================");
    out.println();
}

void TinyCFG::executeActions() const {
    if (!root || !actionHandler) return;
    uint8_t i = 0;
    while (i < root->childCount) {
        const TcfgTaskNode* child = root->children[i];
        if (child->type == TCFG_NODE_ACTION &&
            i + 2 < root->childCount &&
            root->children[i + 1]->type == TCFG_NODE_OPERATOR &&
            root->children[i + 1]->seqOp == TCFG_SEQ_OR &&
            root->children[i + 2]->type == TCFG_NODE_ACTION) {
            actionHandler(child->actionId, child->arg, child->domain, handlerUserData);
            i += 3;
            continue;
        }
        if (child->type == TCFG_NODE_ACTION) {
            actionHandler(child->actionId, child->arg, child->domain, handlerUserData);
        }
        i++;
    }
}

void TinyCFG::dispatchActions(Stream& out) const {
    if (!root || !actionHandler) return;
    uint8_t i = 0;
    while (i < root->childCount) {
        const TcfgTaskNode* child = root->children[i];
        if (child->type == TCFG_NODE_ACTION &&
            i + 2 < root->childCount &&
            root->children[i + 1]->type == TCFG_NODE_OPERATOR &&
            root->children[i + 1]->seqOp == TCFG_SEQ_OR &&
            root->children[i + 2]->type == TCFG_NODE_ACTION) {
            const TcfgTaskNode* alt = root->children[i + 2];
            out.printf("  >> OR choice: %s(%s)  OR  %s(%s) — dispatching first\n",
                actionName(child->actionId), child->arg[0] ? child->arg : "-",
                actionName(alt->actionId), alt->arg[0] ? alt->arg : "-");
            actionHandler(child->actionId, child->arg, child->domain, handlerUserData);
            i += 3;
            continue;
        }
        if (child->type == TCFG_NODE_ACTION) {
            if (i > 0 && root->children[i - 1]->type == TCFG_NODE_OPERATOR &&
                root->children[i - 1]->seqOp == TCFG_SEQ_JUXTA) {
                out.println("  >> (back-to-back command)");
            }
            actionHandler(child->actionId, child->arg, child->domain, handlerUserData);
        }
        i++;
    }
}
