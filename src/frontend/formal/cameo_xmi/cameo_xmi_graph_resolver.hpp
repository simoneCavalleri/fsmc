/**
 * @file cameo_xmi_graph_resolver.hpp
 * @brief Two-pass relational graph resolver for Cameo / MagicDraw OMG UML 2.5 XMI.
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "fsm/frontend/common/xml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal {

/**
 * @class CameoXmiGraphResolver
 * @brief Two-Pass Relational Graph Resolver for OMG UML 2.5 / Cameo Systems Modeler XMI.
 *
 * Implements:
 * - Pass 1: Flat UUID Indexing of all elements while filtering diagram/graphic bloat.
 * - Pass 2: Graph assembly resolving cross-references (xmi:idref, source, target, trigger events).
 * - Stereotype Relinking: Resolves external SysML/MagicDraw profiles linked via base_Element.
 */
class CameoXmiGraphResolver {
  public:
    /**
     * @brief Resolves XML AST into an assembled FsmIr model.
     * @param root Parsed root XML node of the XMI document.
     * @param model Output FsmIr model.
     * @param err Diagnostic message if resolution fails.
     * @return True on success, false otherwise.
     */
    static bool resolve(const std::shared_ptr<XmlNode>& root, fsm::ir::FsmIr& model, std::string& err);

  private:
    static void build_id_map(const std::shared_ptr<XmlNode>& node,
                             std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map);

    static std::shared_ptr<XmlNode> find_state_machine(
        const std::shared_ptr<XmlNode>& root, const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map);

    static void process_region(const std::shared_ptr<XmlNode>& region_node, fsm::ir::FsmIr& model,
                               const std::string& parent_state,
                               const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map,
                               std::unordered_map<std::string, std::string>& id_to_name,
                               std::unordered_map<std::string, fsm::ir::StateKind>& id_to_kind);

    static void process_subvertex(const std::shared_ptr<XmlNode>& vertex_node, fsm::ir::FsmIr& model,
                                  const std::string& parent_state,
                                  const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map,
                                  std::unordered_map<std::string, std::string>& id_to_name,
                                  std::unordered_map<std::string, fsm::ir::StateKind>& id_to_kind);

    static void process_transitions_in_region(const std::shared_ptr<XmlNode>& region_node, fsm::ir::FsmIr& model,
                                              const std::string& parent_state,
                                              const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map,
                                              const std::unordered_map<std::string, std::string>& id_to_name,
                                              const std::unordered_map<std::string, fsm::ir::StateKind>& id_to_kind);

    static void process_transition(const std::shared_ptr<XmlNode>& trans_node, fsm::ir::FsmIr& model,
                                   const std::string& parent_state,
                                   const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map,
                                   const std::unordered_map<std::string, std::string>& id_to_name,
                                   const std::unordered_map<std::string, fsm::ir::StateKind>& id_to_kind);

    static void relink_stereotypes_and_profiles(const std::shared_ptr<XmlNode>& root, fsm::ir::FsmIr& model,
                                                const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map,
                                                const std::unordered_map<std::string, std::string>& id_to_name);
};

}  // namespace fsm::frontend::formal
