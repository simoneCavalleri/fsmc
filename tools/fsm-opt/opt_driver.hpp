#pragma once

#include "tools/fsm-opt/opt_options.hpp"

namespace fsm::tools {

class OptDriver {
  public:
    static int run(const OptOptions& opts);
};

}  // namespace fsm::tools
