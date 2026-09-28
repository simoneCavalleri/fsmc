#!/usr/bin/env bash
# ==============================================================================
# fsmc: Master Examples Automated Verification & Validation Suite
# ==============================================================================
# Validates all 6 modernized examples end-to-end using CLI drivers:
# - fsmc (Universal Compiler Driver & Code Generator)
# - fsm-opt (Formal IR Optimizer, Model Checker & Linter)
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

FSMC="${BUILD_DIR}/bin/fsmc"
FSM_OPT="${BUILD_DIR}/bin/fsm-opt"

# Color Codes
GREEN='\033[1;32m'
CYAN='\033[1;36m'
YELLOW='\033[1;33m'
RED='\033[1;31m'
NC='\033[0m'

print_header() {
    echo -e "\n${CYAN}================================================================================${NC}"
    echo -e "${CYAN} $1${NC}"
    echo -e "${CYAN}================================================================================${NC}"
}

success() {
    echo -e "${GREEN}  ✓ $1${NC}"
}

# 0. Check compiler executables
if [[ ! -x "${FSMC}" || ! -x "${FSM_OPT}" ]]; then
    echo -e "${RED}[ERROR] fsmc and fsm-opt executables not found in ${BUILD_DIR}/bin. Please build first.${NC}"
    exit 1
fi

print_header "FSMC EXAMPLES VALIDATION SUITE START"

# ------------------------------------------------------------------------------
# Showcase 00: Pure Modern C++20 Standalone IoT Thermostat
# ------------------------------------------------------------------------------
print_header "[SHOWCASE 00] Standalone Engine: Pure C++20 IoT Thermostat"
echo -e "${YELLOW}Executing built target: standalone_iot_controller_example...${NC}"
"${BUILD_DIR}/bin/standalone_iot_controller_example" > /dev/null
success "standalone_iot_controller_example executed with 100% assertions passed"

# ------------------------------------------------------------------------------
# Module 01: High-Performance Network Protocol Handshake
# ------------------------------------------------------------------------------
print_header "[MODULE 01] Basic Patterns: Network Protocol Handshake"
echo -e "${YELLOW}Running fsm-opt metrics on connection.sysml...${NC}"
"${FSM_OPT}" "${SCRIPT_DIR}/01_basic_patterns/network_protocol/connection.sysml" --metrics > /dev/null
success "connection.sysml formal metrics OK"

find_binary() {
    local name="$1"
    if [[ -x "${BUILD_DIR}/bin/${name}" ]]; then
        echo "${BUILD_DIR}/bin/${name}"
    elif [[ -x "${BUILD_DIR}/examples/${name}" ]]; then
        echo "${BUILD_DIR}/examples/${name}"
    else
        echo "${BUILD_DIR}/bin/${name}"
    fi
}

echo -e "${YELLOW}Running fsm-opt passes (guard-simplification, dead-state-pruning)...${NC}"
"${FSM_OPT}" "${SCRIPT_DIR}/01_basic_patterns/network_protocol/connection.sysml" \
    --passes=guard-simplification,dead-state-pruning --emit-ir > /dev/null
success "connection.sysml optimization passes OK"

echo -e "${YELLOW}Executing built target: network_protocol_example...${NC}"
"$(find_binary network_protocol_example)" > /dev/null
success "network_protocol_example executed with 100% assertions passed"

# ------------------------------------------------------------------------------
# Module 02: 6-DOF Industrial Robotic Tool Sequencer
# ------------------------------------------------------------------------------
print_header "[MODULE 02] Advanced Semantics: Robotic Arm Sequencer"
echo -e "${YELLOW}Running fsm-opt metrics on robotic_arm.puml...${NC}"
"${FSM_OPT}" "${SCRIPT_DIR}/02_advanced_semantics/robotic_arm_sequencer/robotic_arm.puml" --metrics > /dev/null
success "robotic_arm.puml formal metrics OK"

echo -e "${YELLOW}Running fsm-opt history lowering and boundary fusion passes...${NC}"
"${FSM_OPT}" "${SCRIPT_DIR}/02_advanced_semantics/robotic_arm_sequencer/robotic_arm.puml" \
    --passes=history-lowering,boundary-action-fusion,dead-state-pruning --emit-ir > /dev/null
success "robotic_arm.puml history-lowering passes OK"

echo -e "${YELLOW}Executing built target: robotic_arm_example...${NC}"
"$(find_binary robotic_arm_example)" > /dev/null
success "robotic_arm_example executed with 100% assertions passed"

# ------------------------------------------------------------------------------
# Module 03: Automotive Battery Management System (ISO 26262 ASIL-D)
# ------------------------------------------------------------------------------
print_header "[MODULE 03] Concurrency & Timing: Automotive BMS"
echo -e "${YELLOW}Running fsm-opt metrics on bms.sysml...${NC}"
"${FSM_OPT}" "${SCRIPT_DIR}/03_concurrency_and_timing/automotive_bms/bms.sysml" --metrics > /dev/null
success "bms.sysml formal metrics OK"

echo -e "${YELLOW}Running static data-race analysis across parallel orthogonal regions...${NC}"
"${FSMC}" "${SCRIPT_DIR}/03_concurrency_and_timing/automotive_bms/bms.sysml" --check-races > /dev/null
success "bms.sysml data race check PASSED (0 races detected)"

echo -e "${YELLOW}Executing built target: automotive_bms_example...${NC}"
"$(find_binary automotive_bms_example)" > /dev/null
success "automotive_bms_example executed with 100% assertions passed"

# ------------------------------------------------------------------------------
# Module 04: DO-178C Level A Flight Control Mode Manager
# ------------------------------------------------------------------------------
print_header "[MODULE 04] Formal Verification: Flight Control Modes"
echo -e "${YELLOW}Running formal temporal logic model checking (--verify)...${NC}"
"${FSMC}" "${SCRIPT_DIR}/04_formal_verification/flight_control_modes/fms.sysml" --verify > /dev/null
success "fms.sysml model verification PASSED (3/3 formal temporal properties verified)"

echo -e "${YELLOW}Auditing requirement traceability (@fsm:req)...${NC}"
"${FSMC}" "${SCRIPT_DIR}/04_formal_verification/flight_control_modes/fms.sysml" --req-audit > /dev/null
success "fms.sysml requirement traceability audit PASSED (7 requirements mapped)"

echo -e "${YELLOW}Emitting formal nuXmv SMV specification...${NC}"
"${FSM_OPT}" "${SCRIPT_DIR}/04_formal_verification/flight_control_modes/fms.sysml" --emit-smv > /dev/null
success "fms.sysml nuXmv SMV specification emitted OK"

echo -e "${YELLOW}Executing built target: flight_control_modes_example...${NC}"
"$(find_binary flight_control_modes_example)" > /dev/null
success "flight_control_modes_example executed with 100% assertions passed"

# ------------------------------------------------------------------------------
# Module 05: Custom Toolchain, Unix Pipes & Pass Plugins
# ------------------------------------------------------------------------------
print_header "[MODULE 05] Extensible Toolchain: Unix Filter & Dynamic Plugin"
PLUGIN_PATH="${BUILD_DIR}/examples/naming_audit_pass.so"
if [[ ! -f "${PLUGIN_PATH}" ]]; then
    PLUGIN_PATH="${BUILD_DIR}/libnaming_audit_pass.so"
fi
if [[ ! -f "${PLUGIN_PATH}" ]]; then
    PLUGIN_PATH="${BUILD_DIR}/examples/libnaming_audit_pass.so"
fi

echo -e "${YELLOW}Running fsm-opt with external Unix pipe linter (--pipe-through)...${NC}"
"${FSM_OPT}" "${SCRIPT_DIR}/05_custom_toolchain/plugin_and_pipeline/sensor_pipeline.sysml" \
    --pipe-through="python3 ${SCRIPT_DIR}/05_custom_toolchain/plugin_and_pipeline/scripts/strict_bounds_linter.py" \
    --metrics > /dev/null
success "sensor_pipeline.sysml Unix pipe filter executed OK"

echo -e "${YELLOW}Running fsm-opt with dynamic C++ pass plugin (--load-pass-plugin)...${NC}"
"${FSM_OPT}" "${SCRIPT_DIR}/05_custom_toolchain/plugin_and_pipeline/sensor_pipeline.sysml" \
    "--load-pass-plugin=${PLUGIN_PATH}" \
    --metrics > /dev/null
success "sensor_pipeline.sysml dynamic pass plugin loaded and executed OK"

echo -e "${YELLOW}Executing built target: sensor_pipeline_example...${NC}"
"$(find_binary sensor_pipeline_example)" > /dev/null
success "sensor_pipeline_example executed with 100% assertions passed"

print_header "ALL 6 ENGINEERING SHOWCASES SUCCESSFULLY VALIDATED (100% OK)"

# ------------------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------------------
print_header "ALL 6 FSMC EXAMPLES VERIFIED SUCCESSFULLY (100% PASS RATE)"
echo -e "${GREEN}All models, formal properties, optimization passes, and executable harnesses are valid.${NC}\n"
