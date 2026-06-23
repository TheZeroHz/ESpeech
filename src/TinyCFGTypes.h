#ifndef TINYCFG_TYPES_H
#define TINYCFG_TYPES_H

#include <stdint.h>

#define TCFG_MAX_TOKENS   48
#define TCFG_MAX_TASKS    12
#define TCFG_MAX_ARG_LEN  24
#define TCFG_MAX_TREE_DEPTH 8

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
};

struct TcfgTaskNode {
    TcfgNodeType type;
    uint8_t actionId;
    TcfgSeqOp seqOp;
    char arg[TCFG_MAX_ARG_LEN];
    uint8_t childCount;
    TcfgTaskNode* children[TCFG_MAX_TASKS];
};

struct TcfgParseStats {
    uint8_t tokensConsumed;
    uint8_t tokensTotal;
    uint8_t recoveryCount;
    float confidence;
};

#endif
