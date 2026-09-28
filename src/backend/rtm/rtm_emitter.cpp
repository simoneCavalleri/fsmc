#include "fsm/backend/rtm/rtm_emitter.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

namespace fsm::backend::rtm {

using namespace fsm::ir;

void RtmEmitter::audit_traceability(const FsmIr& ir, DiagnosticEngine& diag) {
    std::size_t untraced_states = 0;
    for (const auto& s : ir.states) {
        if (s.kind == StateKind::Final || ir.is_choice_node(s.name))
            continue;
        if (s.traceability_reqs.empty()) {
            untraced_states++;
            diag.report(Diagnostic::info(
                "I_RTM_UNTRACED", "State '" + s.name + "' has no formal traceability requirement link (@fsm:req)."));
        }
    }
    std::size_t untraced_trans = 0;
    for (const auto& t : ir.transitions) {
        if (t.traceability_reqs.empty()) {
            untraced_trans++;
        }
    }
    diag.report(Diagnostic::info("I_RTM_AUDIT_SUMMARY",
                                 "Traceability Audit: " + std::to_string(ir.states.size() - untraced_states) + "/" +
                                     std::to_string(ir.states.size()) + " states linked, " +
                                     std::to_string(ir.transitions.size() - untraced_trans) + "/" +
                                     std::to_string(ir.transitions.size()) + " transitions linked."));
}

std::string RtmEmitter::emit(const FsmIr& ir, const std::vector<ModelCheckResult>& results, RtmFormat format) {
    auto records = aggregate_records(ir, results);
    if (format == RtmFormat::Json) {
        return emit_json(ir, records);
    }
    return emit_markdown(ir, records);
}

std::string RtmEmitter::emit_json(const FsmIr& ir, const std::vector<RequirementRecord>& records) {
    std::ostringstream ss;
    std::size_t verified_count = 0;
    for (const auto& r : records) {
        if (r.all_properties_passed) {
            ++verified_count;
        }
    }

    double compliance_rate =
        records.empty() ? 100.0 : (static_cast<double>(verified_count) / static_cast<double>(records.size())) * 100.0;

    ss << "{\n";
    ss << "  \"fsm_name\": \"" << escape_json(ir.name) << "\",\n";
    ss << "  \"total_requirements\": " << records.size() << ",\n";
    ss << "  \"verified_requirements\": " << verified_count << ",\n";
    ss << std::fixed << std::setprecision(1);
    ss << "  \"compliance_rate\": " << compliance_rate << ",\n";
    ss << "  \"requirements\": [\n";

    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        ss << "    {\n";
        ss << "      \"id\": \"" << escape_json(r.id) << "\",\n";
        ss << "      \"description\": \"" << escape_json(r.description) << "\",\n";
        ss << "      \"status\": \"" << (r.all_properties_passed ? "VERIFIED" : "VIOLATED") << "\",\n";

        // States
        ss << "      \"covering_states\": [";
        for (std::size_t j = 0; j < r.covering_states.size(); ++j) {
            ss << "\"" << escape_json(r.covering_states[j]) << "\"" << (j + 1 < r.covering_states.size() ? ", " : "");
        }
        ss << "],\n";

        // Transitions
        ss << "      \"covering_transitions\": [";
        for (std::size_t j = 0; j < r.covering_transitions.size(); ++j) {
            ss << "\"" << escape_json(r.covering_transitions[j]) << "\""
               << (j + 1 < r.covering_transitions.size() ? ", " : "");
        }
        ss << "],\n";

        // Formal Properties
        ss << "      \"formal_properties\": [";
        for (std::size_t j = 0; j < r.formal_properties.size(); ++j) {
            ss << "\"" << escape_json(r.formal_properties[j]) << "\""
               << (j + 1 < r.formal_properties.size() ? ", " : "");
        }
        ss << "]\n";

        ss << "    }" << (i + 1 < records.size() ? "," : "") << "\n";
    }

    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

std::string RtmEmitter::emit_markdown(const FsmIr& ir, const std::vector<RequirementRecord>& records) {
    std::ostringstream ss;
    std::size_t verified_count = 0;
    for (const auto& r : records) {
        if (r.all_properties_passed) {
            ++verified_count;
        }
    }

    double compliance_rate =
        records.empty() ? 100.0 : (static_cast<double>(verified_count) / static_cast<double>(records.size())) * 100.0;

    ss << "# Requirement Traceability Matrix (RTM): " << ir.name << "\n\n";
    ss << std::fixed << std::setprecision(1);
    ss << "**Verification Compliance:** " << compliance_rate << "% (" << verified_count << "/" << records.size()
       << " Requirements Verified)\n\n";

    ss << "| Requirement ID | Description | Covered States | Covered Transitions | Formal Properties | Status |\n";
    ss << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";

    if (records.empty()) {
        ss << "| *None* | *No traceability tags defined* | - | - | - | **N/A** |\n";
        return ss.str();
    }

    for (const auto& r : records) {
        ss << "| `" << r.id << "` | " << (r.description.empty() ? "-" : r.description) << " | ";

        // States
        if (r.covering_states.empty()) {
            ss << "-";
        } else {
            for (std::size_t j = 0; j < r.covering_states.size(); ++j) {
                ss << "`" << r.covering_states[j] << "`" << (j + 1 < r.covering_states.size() ? ", " : "");
            }
        }
        ss << " | ";

        // Transitions
        if (r.covering_transitions.empty()) {
            ss << "-";
        } else {
            for (std::size_t j = 0; j < r.covering_transitions.size(); ++j) {
                ss << "`" << r.covering_transitions[j] << "`" << (j + 1 < r.covering_transitions.size() ? ", " : "");
            }
        }
        ss << " | ";

        // Formal Properties
        if (r.formal_properties.empty()) {
            ss << "-";
        } else {
            for (std::size_t j = 0; j < r.formal_properties.size(); ++j) {
                ss << "`" << r.formal_properties[j] << "`" << (j + 1 < r.formal_properties.size() ? ", " : "");
            }
        }
        ss << " | ";

        // Status badge
        ss << (r.all_properties_passed ? "**PASSED**" : "**VIOLATED**") << " |\n";
    }

    return ss.str();
}

std::vector<RequirementRecord> RtmEmitter::aggregate_records(const FsmIr& ir,
                                                             const std::vector<ModelCheckResult>& results) {
    std::map<std::string, RequirementRecord> map;

    // 1. From States
    for (const auto& s : ir.states) {
        for (const auto& req : s.traceability_reqs) {
            if (req.empty())
                continue;
            auto& rec = map[req];
            rec.id = req;
            if (std::find(rec.covering_states.begin(), rec.covering_states.end(), s.name) ==
                rec.covering_states.end()) {
                rec.covering_states.push_back(s.name);
            }
        }
    }

    // 2. From Transitions
    for (const auto& t : ir.transitions) {
        for (const auto& req : t.traceability_reqs) {
            if (req.empty())
                continue;
            auto& rec = map[req];
            rec.id = req;
            std::string t_label = t.source + " -> " + t.target;
            if (std::find(rec.covering_transitions.begin(), rec.covering_transitions.end(), t_label) ==
                rec.covering_transitions.end()) {
                rec.covering_transitions.push_back(t_label);
            }
        }
    }

    // 3. From Formal Properties
    for (const auto& p : ir.properties) {
        if (p.traceability_req.empty())
            continue;
        auto& rec = map[p.traceability_req];
        rec.id = p.traceability_req;
        if (rec.description.empty() && !p.description.empty()) {
            rec.description = p.description;
        }
        if (std::find(rec.formal_properties.begin(), rec.formal_properties.end(), p.name) ==
            rec.formal_properties.end()) {
            rec.formal_properties.push_back(p.name);
        }
    }

    // 4. Correlate with verification results
    for (auto& [req_id, rec] : map) {
        for (const auto& prop_name : rec.formal_properties) {
            for (const auto& res : results) {
                if (res.property_name == prop_name && !res.passed) {
                    rec.all_properties_passed = false;
                    break;
                }
            }
        }
    }

    std::vector<RequirementRecord> result;
    result.reserve(map.size());
    for (auto& [_, rec] : map) {
        result.push_back(std::move(rec));
    }
    return result;
}

std::string RtmEmitter::escape_json(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        if (c == '"')
            ss << "\\\"";
        else if (c == '\\')
            ss << "\\\\";
        else if (c == '\n')
            ss << "\\n";
        else if (c == '\r')
            ss << "\\r";
        else if (c == '\t')
            ss << "\\t";
        else
            ss << c;
    }
    return ss.str();
}

}  // namespace fsm::backend::rtm
