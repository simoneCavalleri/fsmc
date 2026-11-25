#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "fsm/frontend/formal/sysml2/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal::sysml2_detail {

struct PortDefDataFeature {
    fsm::ir::PortDirection direction{fsm::ir::PortDirection::In};
    std::string name;
    std::string raw_type;
};

struct PortDefItemFeature {
    fsm::ir::PortDirection direction{fsm::ir::PortDirection::In};
    std::string name;
    std::string signal_type;
};

struct Sysml2PortDefSpec {
    std::string name;
    std::vector<PortDefDataFeature> data_features;
    std::vector<PortDefItemFeature> item_features;
};

struct PortInstantiation {
    std::string instance_name;
    std::string port_def_name;
    bool conjugated{false};
};

struct WorkspaceMetadata {
    std::unordered_map<std::string, Sysml2PortDefSpec> port_defs;
    /// state_def_name -> port instantiations (from `part def { port ...; exhibit state ... : StateDef; }` or direct)
    std::unordered_map<std::string, std::vector<PortInstantiation>> state_def_ports;
};

std::vector<std::string> scan_state_def_names(std::string_view content);

bool resolve_workspace_imports(std::string_view content, const Sysml2ParseOptions& opts, std::string& out_combined,
                               std::string& out_error);

WorkspaceMetadata extract_port_defs_and_bindings(std::string_view combined_content);

void apply_port_bindings_to_model(const WorkspaceMetadata& meta, const std::string& state_def_name,
                                  fsm::ir::FsmIr& model, const Sysml2SymbolResolver& resolver);

}  // namespace fsm::frontend::formal::sysml2_detail
