#include "tools/fsm-opt/opt_driver.hpp"
#include "tools/fsm-opt/opt_options.hpp"

int main(int argc, char* argv[]) {
    const auto opts = fsm::tools::parse_opt_args(argc, argv);
    return fsm::tools::OptDriver::run(opts);
}
