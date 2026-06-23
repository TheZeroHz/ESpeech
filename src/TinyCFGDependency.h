#ifndef TINYCFG_DEPENDENCY_H
#define TINYCFG_DEPENDENCY_H

#include <Arduino.h>
#include "TinyCFGTypes.h"

class TinyCFGDependency {
public:
  static void analyze(const TcfgTaskNode* root, TcfgDependencyReport& report);

  static void printReport(const TcfgDependencyReport& report, Stream& out);
};

#endif
