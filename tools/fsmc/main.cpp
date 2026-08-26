#include "tools/fsmc/fsmc_driver.hpp"
#include "tools/fsmc/fsmc_options.hpp"

int main(int argc, char* argv[]) {
    const auto opts = fsm::tools::parse_cli_args(argc, argv);
    return fsm::tools::FsmcDriver::run(opts);
}
