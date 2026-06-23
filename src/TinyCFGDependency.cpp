#include "TinyCFGDependency.h"
#include <string.h>

static uint8_t collectActionNodes(const TcfgTaskNode* root, const TcfgTaskNode** out, uint8_t maxOut) {
    if (!root || maxOut == 0) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < root->childCount && n < maxOut; ++i) {
        const TcfgTaskNode* c = root->children[i];
        if (c->type == TCFG_NODE_ACTION) {
            out[n++] = c;
        }
    }
    return n;
}

void TinyCFGDependency::analyze(const TcfgTaskNode* root, TcfgDependencyReport& report) {
    memset(&report, 0, sizeof(report));
    if (!root) return;

    const TcfgTaskNode* tasks[TCFG_MAX_TASKS];
    report.taskCount = collectActionNodes(root, tasks, TCFG_MAX_TASKS);

    TcfgSeqOp lastOp = TCFG_SEQ_NONE;
    for (uint8_t i = 0; i < root->childCount; ++i) {
        const TcfgTaskNode* c = root->children[i];
        if (c->type == TCFG_NODE_OPERATOR) {
            lastOp = c->seqOp;
        } else if (c->type == TCFG_NODE_ACTION && report.depCount < TCFG_MAX_DEPS) {
            if (i > 0 && lastOp != TCFG_SEQ_NONE && lastOp != TCFG_SEQ_OR) {
                TcfgDependency& d = report.deps[report.depCount++];
                d.fromTask = report.taskCount > 1 ? report.taskCount - 2 : 0;
                d.toTask = report.taskCount - 1;
                d.relation = lastOp;
            }
        }
    }

    for (uint8_t i = 0; i < report.taskCount; ++i) {
        for (uint8_t j = i + 1; j < report.taskCount; ++j) {
            const TcfgTaskNode* a = tasks[i];
            const TcfgTaskNode* b = tasks[j];
            if (!a || !b) continue;

            if (a->resource == b->resource && a->resource == 6 &&
                a->actionId != b->actionId) {
                if (report.conflictCount < TCFG_MAX_CONFLICTS) {
                    TcfgConflict& c = report.conflicts[report.conflictCount++];
                    c.type = TCFG_CONFLICT_NAVIGATION;
                    c.taskA = i;
                    c.taskB = j;
                    strncpy(c.description, "concurrent navigation commands", sizeof(c.description) - 1);
                }
            }
        }
    }

    report.executionSafe = (report.conflictCount == 0);
}

void TinyCFGDependency::printReport(const TcfgDependencyReport& report, Stream& out) {
    out.println("--- Command Dependency Analysis (CDA) ---");
    out.printf("Tasks: %u  Dependencies: %u  Conflicts: %u\n",
        report.taskCount, report.depCount, report.conflictCount);
    out.printf("Execution safe: %s\n", report.executionSafe ? "YES" : "NO");

    for (uint8_t i = 0; i < report.depCount; ++i) {
        const TcfgDependency& d = report.deps[i];
        out.printf("  DEP[%u]: task%u --(%u)--> task%u\n",
            i, d.fromTask, (unsigned)d.relation, d.toTask);
    }
    for (uint8_t i = 0; i < report.conflictCount; ++i) {
        const TcfgConflict& c = report.conflicts[i];
        out.printf("  CONFLICT[%u]: type=%u tasks %u<->%u : %s\n",
            i, (unsigned)c.type, c.taskA, c.taskB, c.description);
    }
}
