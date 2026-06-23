#ifndef TINYCFG_TYPES_H
#define TINYCFG_TYPES_H

#include <stdint.h>

#define TCFG_MAX_TOKENS       64
#define TCFG_MAX_TASKS        16
#define TCFG_MAX_ARG_LEN      48
#define TCFG_MAX_TOKEN_TEXT   24
#define TCFG_MAX_TREE_DEPTH   8
#define TCFG_MAX_DEPS         8
#define TCFG_MAX_CONFLICTS    8

enum TcfgNodeType : uint8_t {
    TCFG_NODE_TASK_LIST = 0,
    TCFG_NODE_ACTION    = 1,
    TCFG_NODE_OPERATOR  = 2,
};

enum TcfgSeqOp : uint8_t {
    TCFG_SEQ_NONE   = 0,
    TCFG_SEQ_AND    = 1,
    TCFG_SEQ_THEN   = 2,
    TCFG_SEQ_AFTER  = 3,
    TCFG_SEQ_BEFORE = 4,
    TCFG_SEQ_IF     = 5,
    TCFG_SEQ_UNTIL  = 6,
    TCFG_SEQ_WHILE  = 7,
    TCFG_SEQ_OR     = 8,   // alternative choice (execute one)
    TCFG_SEQ_JUXTA  = 9,   // implicit back-to-back commands (no connector word)
};

enum TcfgConflictType : uint8_t {
    TCFG_CONFLICT_NONE       = 0,
    TCFG_CONFLICT_RESOURCE   = 1,  // same resource, incompatible actions
    TCFG_CONFLICT_NAVIGATION = 2,  // concurrent nav commands
    TCFG_CONFLICT_SECURITY   = 3,  // arm/disarm race
};

struct TcfgTaskNode {
    TcfgNodeType type;
    uint8_t actionId;
    uint8_t domain;
    uint8_t resource;
    TcfgSeqOp seqOp;
    char arg[TCFG_MAX_ARG_LEN];
    uint8_t childCount;
    TcfgTaskNode* children[TCFG_MAX_TASKS];
};

struct TcfgDependency {
    uint8_t fromTask;
    uint8_t toTask;
    TcfgSeqOp relation;
};

struct TcfgConflict {
    TcfgConflictType type;
    uint8_t taskA;
    uint8_t taskB;
    char description[48];
};

struct TcfgDependencyReport {
    uint8_t taskCount;
    uint8_t depCount;
    TcfgDependency deps[TCFG_MAX_DEPS];
    uint8_t conflictCount;
    TcfgConflict conflicts[TCFG_MAX_CONFLICTS];
    bool executionSafe;
};

#define TCFG_MAX_NOISE_SKIP   12
#define TCFG_TOKEN_EXACT  0
#define TCFG_TOKEN_FUZZY  1
#define TCFG_TOKEN_PHON   2

struct TcfgParseStats {
    uint8_t tokensConsumed;
    uint8_t tokensTotal;
    uint8_t recoveryCount;
    uint8_t phoneticHits;
    uint8_t patternMatches;
    uint8_t noiseSkipped;
    uint8_t gluedSegments;
    float confidence;
    uint32_t parseLatencyUs;
    uint16_t ramUsedBytes;
};

#endif
