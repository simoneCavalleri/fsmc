#pragma once

#include "tools/fsmc/fsmc_options.hpp"

namespace fsm::tools {

class FsmcDriver {
  public:
    static int run(const FsmcOptions& opts);
};

}  // namespace fsm::tools
