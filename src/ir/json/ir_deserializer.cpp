#include <cmath>
#include <cstdlib>
#include <unordered_set>

#include "fsm/ir/formal_property.hpp"
#include "fsm/ir/fsm_ir_deserializer.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/identifier_utils.hpp"
#include "fsm/ir/json_dom.hpp"
#include "ir_deserializer_internal.hpp"

namespace fsm::ir {

using namespace json_detail;

// ============================================================================
// FsmIrDeserializer Public API Implementation
// ============================================================================

bool FsmIrDeserializer::deserialize_json(std::string_view json_content, FsmIr& out_ir, std::string& out_error) {
    return deserialize_json(json_content, out_ir, out_error, DeserializeOptions{});
}

bool FsmIrDeserializer::deserialize_json(std::string_view json_content, FsmIr& out_ir, std::string& out_error,
                                         const DeserializeOptions& options) {
    JsonValue root;
    if (!SimpleJsonParser::parse(json_content, root, out_error)) {
        out_error = "JSON Deserializer: Failed to parse JSON: " + out_error;
        return false;
    }

    if (!root.is_object()) {
        out_error = "JSON Deserializer: Root JSON must be an object.";
        return false;
    }

    if (options.strict_ir_json) {
        const std::string schema_ver = root.get_string("ir_schema_version");
        if (schema_ver.empty()) {
            out_error = "E_IR_SCHEMA_VERSION: Missing required root field 'ir_schema_version' (expected \"1.0\").";
            return false;
        }
        if (schema_ver != "1.0") {
            out_error = "E_IR_SCHEMA_VERSION: Unsupported 'ir_schema_version' '" + schema_ver + "' (expected \"1.0\").";
            return false;
        }

        static const std::unordered_set<std::string_view> kAllowedIrRootKeys = {"ir_schema_version",
                                                                                "id",
                                                                                "fsm_id",
                                                                                "name",
                                                                                "initial",
                                                                                "initial_state",
                                                                                "initial_state_id",
                                                                                "package",
                                                                                "satisfies_reqs",
                                                                                "attributes",
                                                                                "concurrency",
                                                                                "execution_semantics",
                                                                                "ports",
                                                                                "variables",
                                                                                "enums",
                                                                                "structs",
                                                                                "types",
                                                                                "custom_types",
                                                                                "signals",
                                                                                "guards",
                                                                                "actions",
                                                                                "clocks",
                                                                                "choice_nodes",
                                                                                "properties",
                                                                                "states",
                                                                                "transitions",
                                                                                "bindings",
                                                                                "services",
                                                                                "source_file"};
        for (const auto& key : root.obj_keys) {
            if (kAllowedIrRootKeys.find(key) == kAllowedIrRootKeys.end()) {
                out_error = "E_IR_UNKNOWN_FIELD: Unrecognized root key '" + key + "' in strict ir-json input.";
                return false;
            }
        }
        if (root.get_child("states") == nullptr) {
            out_error = "E_IR_SCHEMA_VERSION: Missing required root field 'states' in strict ir-json input.";
            return false;
        }
    }

    std::string fsm_id = root.get_string("id");
    std::string explicit_fsm_id = root.get_string("fsm_id");
    if (!explicit_fsm_id.empty()) {
        out_ir.id = explicit_fsm_id;
    } else if (!fsm_id.empty()) {
        out_ir.id = fsm_id;
    }
    if (!fsm_id.empty()) {
        out_ir.name = sanitize_identifier(fsm_id);
    }
    std::string sm_name = root.get_string("name");
    if (!sm_name.empty()) {
        out_ir.name = sanitize_identifier(sm_name);
    }

    std::string init_state = root.get_string("initial");
    if (!init_state.empty()) {
        out_ir.initial_state = sanitize_identifier(init_state);
    }
    std::string init_state_id = root.get_string("initial_state_id");
    if (!init_state_id.empty()) {
        out_ir.initial_state_id = init_state_id;
    }

    std::string pkg = root.get_string("package");
    if (!pkg.empty()) {
        out_ir.package = pkg;
    }

    const auto* sat_arr = root.get_child("satisfies_reqs");
    if (sat_arr == nullptr) {
        sat_arr = root.get_child("satisfies");
    }
    if (sat_arr != nullptr && sat_arr->is_array()) {
        for (const auto& r_item : sat_arr->arr_val) {
            if (r_item.is_string()) {
                out_ir.satisfies_reqs.push_back(r_item.str_val);
            }
        }
    }

    const auto* attrs_obj = root.get_child("attributes");
    if (attrs_obj != nullptr && attrs_obj->is_object()) {
        for (std::size_t i = 0; i < attrs_obj->obj_keys.size(); ++i) {
            const auto& k = attrs_obj->obj_keys[i];
            const auto& v = attrs_obj->obj_values[i];
            if (v.is_string()) {
                out_ir.attributes[k] = v.str_val;
            }
        }
    }

    // Concurrency
    const auto* conc_obj = root.get_child("concurrency");
    if (conc_obj != nullptr && conc_obj->is_object()) {
        std::string disp = conc_obj->get_string("dispatch");
        if (!disp.empty()) {
            out_ir.concurrency.dispatch = string_to_event_dispatch_semantics(disp);
        }
        std::string oc = conc_obj->get_string("orthogonal_conflict");
        if (!oc.empty()) {
            out_ir.concurrency.orthogonal_conflict = string_to_orthogonal_conflict_resolution(oc);
        }
        std::string di = conc_obj->get_string("datapath_isolation");
        if (!di.empty()) {
            out_ir.concurrency.datapath_isolation = string_to_datapath_isolation(di);
        }
    }

    // Variables
    const auto* vars_arr = root.get_child("variables");
    if (vars_arr != nullptr && vars_arr->is_array()) {
        for (const auto& v_val : vars_arr->arr_val) {
            if (v_val.is_object()) {
                VariableDefinition var;
                var.name = sanitize_identifier(v_val.get_string("name"));
                var.type = v_val.get_string("type", "uint32_t");
                var.initial_value = v_val.get_string("init", v_val.get_string("initial_value", "0"));
                std::string v_unit = v_val.get_string("physical_unit", v_val.get_string("unit"));
                if (!v_unit.empty()) {
                    var.physical_unit = v_unit;
                }
                if (const auto* min_c = v_val.get_child("min_value")) {
                    if (min_c->type == JsonType::Number) {
                        var.min_value = static_cast<int64_t>(min_c->num_val);
                    }
                } else if (const auto* min_c2 = v_val.get_child("min")) {
                    if (min_c2->type == JsonType::Number) {
                        var.min_value = static_cast<int64_t>(min_c2->num_val);
                    }
                }
                if (const auto* max_c = v_val.get_child("max_value")) {
                    if (max_c->type == JsonType::Number) {
                        var.max_value = static_cast<int64_t>(max_c->num_val);
                    }
                } else if (const auto* max_c2 = v_val.get_child("max")) {
                    if (max_c2->type == JsonType::Number) {
                        var.max_value = static_cast<int64_t>(max_c2->num_val);
                    }
                }
                if (const auto* reg_c = v_val.get_child("register_index")) {
                    if (reg_c->type == JsonType::Number) {
                        var.register_index = static_cast<std::size_t>(reg_c->num_val);
                    }
                }
                var.description = v_val.get_string("description", v_val.get_string("desc"));
                if (!var.name.empty()) {
                    out_ir.add_variable(std::move(var));
                }
            }
        }
    }

    // XState-style "context": { "var": initial_value, ... }
    const auto* ctx_obj = root.get_child("context");
    if (ctx_obj != nullptr && ctx_obj->is_object()) {
        for (std::size_t i = 0; i < ctx_obj->obj_keys.size(); ++i) {
            const auto& raw_name = ctx_obj->obj_keys[i];
            const auto& val = ctx_obj->obj_values[i];
            std::string name = sanitize_identifier(raw_name);
            if (name.empty() || out_ir.find_variable(name) != nullptr) {
                continue;
            }
            VariableDefinition var;
            var.name = std::move(name);
            if (val.type == JsonType::Bool) {
                var.type = "bool";
                var.initial_value = val.bool_val ? "true" : "false";
            } else if (val.type == JsonType::Number) {
                double int_part = 0.0;
                if (std::modf(val.num_val, &int_part) == 0.0) {
                    var.type = "int32_t";
                    var.initial_value = std::to_string(static_cast<long long>(val.num_val));
                } else {
                    var.type = "double";
                    var.initial_value = std::to_string(val.num_val);
                }
            } else if (val.type == JsonType::String) {
                var.type = "std::string";
                var.initial_value = "\"" + val.str_val + "\"";
            } else {
                var.type = "int32_t";
                var.initial_value = "0";
            }
            out_ir.add_variable(std::move(var));
        }
    }

    // Ports
    const auto* ports_arr = root.get_child("ports");
    if (ports_arr != nullptr && ports_arr->is_array()) {
        for (const auto& p_val : ports_arr->arr_val) {
            if (p_val.is_object()) {
                PortDefinition port;
                port.name = sanitize_identifier(p_val.get_string("name"));
                port.type = p_val.get_string("type", "float");
                std::string dir_str = p_val.get_string("direction", "in");
                port.direction = string_to_port_direction(dir_str);
                if (const auto* min_c = p_val.get_child("min")) {
                    if (min_c->type == JsonType::Number)
                        port.min_value = min_c->num_val;
                } else if (const auto* min_c2 = p_val.get_child("min_value")) {
                    if (min_c2->type == JsonType::Number)
                        port.min_value = min_c2->num_val;
                }
                if (const auto* max_c = p_val.get_child("max")) {
                    if (max_c->type == JsonType::Number)
                        port.max_value = max_c->num_val;
                } else if (const auto* max_c2 = p_val.get_child("max_value")) {
                    if (max_c2->type == JsonType::Number)
                        port.max_value = max_c2->num_val;
                }
                port.constraint = p_val.get_string("constraint");
                port.default_value = p_val.get_string("default_value", p_val.get_string("default"));
                std::string p_unit = p_val.get_string("physical_unit", p_val.get_string("unit"));
                if (!p_unit.empty()) {
                    port.physical_unit = p_unit;
                }
                port.description = p_val.get_string("description", p_val.get_string("desc"));
                port.bound_to = p_val.get_string("bound_to");
                if (!port.name.empty()) {
                    out_ir.ports.push_back(std::move(port));
                }
            }
        }
    }

    // Signals
    const auto* sigs_arr = root.get_child("signals");
    if (sigs_arr != nullptr && sigs_arr->is_array()) {
        for (const auto& s_val : sigs_arr->arr_val) {
            if (s_val.is_object()) {
                SignalDefinition sig;
                sig.name = sanitize_identifier(s_val.get_string("name"));
                sig.description = s_val.get_string("description", s_val.get_string("desc"));
                const auto* attrs_arr = s_val.get_child("attributes");
                if (attrs_arr != nullptr && attrs_arr->is_array()) {
                    for (const auto& a_val : attrs_arr->arr_val) {
                        if (a_val.is_object()) {
                            SignalAttribute attr;
                            attr.name = sanitize_identifier(a_val.get_string("name"));
                            attr.type = a_val.get_string("type", "uint32_t");
                            attr.default_value = a_val.get_string("default", a_val.get_string("default_value"));
                            sig.attributes.push_back(std::move(attr));
                        }
                    }
                }
                if (const auto* val_arr = s_val.get_child("validators")) {
                    if (val_arr->is_array()) {
                        for (const auto& v_item : val_arr->arr_val) {
                            if (v_item.is_string()) {
                                sig.validators.push_back(v_item.str_val);
                            }
                        }
                    }
                }
                if (!sig.name.empty()) {
                    out_ir.signals.push_back(std::move(sig));
                }
            }
        }
    }

    // Guards
    const auto* guards_arr = root.get_child("guards");
    if (guards_arr != nullptr && guards_arr->is_array()) {
        for (const auto& g_val : guards_arr->arr_val) {
            if (g_val.is_object()) {
                std::string g_name = g_val.get_string("name");
                if (g_name.empty()) {
                    continue;
                }
                std::string g_desc = g_val.get_string("description");
                std::optional<std::string> raw_expr;
                if (const auto* r = g_val.get_child("raw_expression"); r != nullptr && r->is_string()) {
                    raw_expr = r->str_val;
                }
                std::optional<std::string> norm_expr;
                if (const auto* n = g_val.get_child("normalized_expression"); n != nullptr && n->is_string()) {
                    norm_expr = n->str_val;
                }
                out_ir.add_guard(g_name, std::move(g_desc), std::move(raw_expr), std::move(norm_expr));
            }
        }
    }

    // Clocks
    const auto* services_arr = root.get_child("services");
    if (services_arr != nullptr && services_arr->is_array()) {
        for (const auto& s_val : services_arr->arr_val) {
            if (!s_val.is_object()) {
                continue;
            }
            ServiceMethodSignature svc;
            svc.name = sanitize_identifier(s_val.get_string("name"));
            const std::string ret_str = s_val.get_string("return_type", "void");
            svc.return_type =
                (ret_str.empty() || ret_str == "void") ? DataType(PrimitiveTypeKind::Void) : DataType(ret_str);
            if (const auto* pure_c = s_val.get_child("pure"); pure_c != nullptr && pure_c->type == JsonType::Bool) {
                svc.is_pure = pure_c->bool_val;
            }
            if (const auto* ne_c = s_val.get_child("noexcept"); ne_c != nullptr && ne_c->type == JsonType::Bool) {
                svc.is_noexcept = ne_c->bool_val;
            }
            svc.description = s_val.get_string("description");
            if (const auto* params_arr = s_val.get_child("params"); params_arr != nullptr && params_arr->is_array()) {
                for (const auto& p_val : params_arr->arr_val) {
                    if (!p_val.is_object()) {
                        continue;
                    }
                    const std::string pname = sanitize_identifier(p_val.get_string("name"));
                    const std::string ptype = p_val.get_string("type", "int32_t");
                    if (!pname.empty()) {
                        svc.parameters.emplace_back(pname, DataType(ptype));
                    }
                }
            }
            if (!svc.name.empty()) {
                out_ir.add_service(std::move(svc));
            }
        }
    }

    const auto* clocks_arr = root.get_child("clocks");
    if (clocks_arr != nullptr && clocks_arr->is_array()) {
        for (const auto& c_val : clocks_arr->arr_val) {
            if (c_val.is_object()) {
                std::string c_name = sanitize_identifier(c_val.get_string("name"));
                if (c_name.empty()) {
                    continue;
                }
                bool exists = false;
                for (const auto& existing : out_ir.clocks) {
                    if (existing.name == c_name) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    TimeUnit res = time_unit_from_string(c_val.get_string("resolution", "us"));
                    std::string desc = c_val.get_string("description");
                    ClockDefinition clk(c_name, res, desc);
                    std::string c_id = c_val.get_string("id");
                    if (!c_id.empty()) {
                        clk.id = c_id;
                    }
                    out_ir.clocks.push_back(std::move(clk));
                }
            } else if (c_val.is_string()) {
                std::string c_name = sanitize_identifier(c_val.str_val);
                if (!c_name.empty()) {
                    bool exists = false;
                    for (const auto& existing : out_ir.clocks) {
                        if (existing.name == c_name) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        out_ir.clocks.emplace_back(c_name);
                    }
                }
            }
        }
    }

    // Choice Nodes
    const auto* choice_arr = root.get_child("choice_nodes");
    if (choice_arr != nullptr && choice_arr->is_array()) {
        for (const auto& cn_val : choice_arr->arr_val) {
            std::string cn_name;
            if (cn_val.is_object()) {
                cn_name = sanitize_identifier(cn_val.get_string("name"));
            } else if (cn_val.is_string()) {
                cn_name = sanitize_identifier(cn_val.str_val);
            }
            if (!cn_name.empty() && !out_ir.is_choice_node(cn_name)) {
                out_ir.add_choice_node(cn_name);
            }
        }
    }

    // Enums
    const auto* enums_arr = root.get_child("enums");
    if (enums_arr != nullptr && enums_arr->is_array()) {
        for (const auto& e_val : enums_arr->arr_val) {
            if (e_val.is_object()) {
                std::string e_name = sanitize_identifier(e_val.get_string("name"));
                std::string e_type = e_val.get_string("underlying_type", e_val.get_string("type", "uint32_t"));
                EnumDefinition en(e_name, e_type);
                en.description = e_val.get_string("description");
                const auto* lits_arr = e_val.get_child("literals");
                if (lits_arr != nullptr && lits_arr->is_array()) {
                    for (const auto& l_val : lits_arr->arr_val) {
                        if (l_val.is_object()) {
                            std::string l_name = sanitize_identifier(l_val.get_string("name"));
                            std::optional<int64_t> l_num;
                            if (const auto* v_num = l_val.get_child("value")) {
                                if (v_num->type == JsonType::Number) {
                                    l_num = static_cast<int64_t>(v_num->num_val);
                                }
                            }
                            if (!l_name.empty()) {
                                EnumLiteral el(l_name, l_num);
                                el.description = l_val.get_string("description");
                                en.add_literal(std::move(el));
                            }
                        }
                    }
                }
                if (!en.name.empty()) {
                    out_ir.add_enum(std::move(en));
                }
            }
        }
    }

    // Structs
    const auto* structs_arr = root.get_child("structs");
    if (structs_arr != nullptr && structs_arr->is_array()) {
        for (const auto& s_val : structs_arr->arr_val) {
            if (s_val.is_object()) {
                std::string st_name = sanitize_identifier(s_val.get_string("name"));
                StructDefinition st(st_name);
                st.description = s_val.get_string("description");
                if (const auto* dt_val = s_val.get_child("is_datatype")) {
                    st.is_datatype = (dt_val->type == JsonType::Bool && dt_val->bool_val);
                }
                const auto* fields_arr = s_val.get_child("fields");
                if (fields_arr != nullptr && fields_arr->is_array()) {
                    for (const auto& f_val : fields_arr->arr_val) {
                        if (f_val.is_object()) {
                            std::string f_name = sanitize_identifier(f_val.get_string("name"));
                            std::string f_type = f_val.get_string("type", "string");
                            std::string f_def = f_val.get_string("default_value", f_val.get_string("default"));
                            std::string f_unit = f_val.get_string("physical_unit");
                            std::optional<double> f_min;
                            if (const auto* m_val = f_val.get_child("min_value")) {
                                if (m_val->type == JsonType::Number)
                                    f_min = m_val->num_val;
                            }
                            std::optional<double> f_max;
                            if (const auto* m_val = f_val.get_child("max_value")) {
                                if (m_val->type == JsonType::Number)
                                    f_max = m_val->num_val;
                            }
                            std::string f_desc = f_val.get_string("description");
                            if (!f_name.empty()) {
                                StructField sf(f_name, f_type, f_def);
                                if (!f_unit.empty())
                                    sf.physical_unit = f_unit;
                                sf.min_value = f_min;
                                sf.max_value = f_max;
                                sf.description = f_desc;
                                st.add_field(std::move(sf));
                            }
                        }
                    }
                }
                if (!st.name.empty()) {
                    out_ir.add_struct(std::move(st));
                }
            }
        }
    }

    // Types
    const auto* types_arr = root.get_child("types");
    if (types_arr != nullptr && types_arr->is_array()) {
        for (const auto& t_val : types_arr->arr_val) {
            if (t_val.is_object()) {
                TypeDefinition td;
                td.name = sanitize_identifier(t_val.get_string("name"));
                std::string kind_str = t_val.get_string("kind", "struct");
                td.kind = string_to_type_kind(kind_str);
                td.underlying_type = t_val.get_string("underlying_type");
                td.description = t_val.get_string("description");
                if (const auto* dt_val = t_val.get_child("is_datatype")) {
                    td.is_datatype = (dt_val->type == JsonType::Bool && dt_val->bool_val);
                }
                if (const auto* ext_ref = t_val.get_child("external_ref"); ext_ref != nullptr && ext_ref->is_object()) {
                    const std::string ext_mod = ext_ref->get_string("module");
                    const std::string ext_sym = ext_ref->get_string("symbol");
                    if (!ext_mod.empty() || !ext_sym.empty()) {
                        td.external_ref = ExternalTypeRef{ext_mod, ext_sym.empty() ? td.name : ext_sym};
                    }
                }
                const auto* lits_arr = t_val.get_child("literals");
                if (lits_arr != nullptr && lits_arr->is_array()) {
                    for (const auto& l_val : lits_arr->arr_val) {
                        if (l_val.is_object()) {
                            std::string l_name = sanitize_identifier(l_val.get_string("name"));
                            std::optional<int64_t> l_num;
                            if (const auto* v_num = l_val.get_child("value")) {
                                if (v_num->type == JsonType::Number) {
                                    l_num = static_cast<int64_t>(v_num->num_val);
                                }
                            }
                            std::string l_desc = l_val.get_string("description");
                            if (!l_name.empty()) {
                                td.add_literal(l_name, l_num, l_desc);
                            }
                        }
                    }
                }
                const auto* fields_arr = t_val.get_child("fields");
                if (fields_arr != nullptr && fields_arr->is_array()) {
                    for (const auto& f_val : fields_arr->arr_val) {
                        if (f_val.is_object()) {
                            std::string f_name = sanitize_identifier(f_val.get_string("name"));
                            std::string f_type = f_val.get_string("type", "string");
                            std::string f_def = f_val.get_string("default_value", f_val.get_string("default"));
                            std::string f_unit_str = f_val.get_string("physical_unit", f_val.get_string("unit"));
                            std::optional<std::string> f_unit =
                                f_unit_str.empty() ? std::nullopt : std::make_optional(f_unit_str);
                            std::optional<double> f_min;
                            if (const auto* m_val = f_val.get_child("min_value")) {
                                if (m_val->type == JsonType::Number)
                                    f_min = m_val->num_val;
                            } else if (const auto* m_val2 = f_val.get_child("min")) {
                                if (m_val2->type == JsonType::Number)
                                    f_min = m_val2->num_val;
                            }
                            std::optional<double> f_max;
                            if (const auto* m_val = f_val.get_child("max_value")) {
                                if (m_val->type == JsonType::Number)
                                    f_max = m_val->num_val;
                            } else if (const auto* m_val2 = f_val.get_child("max")) {
                                if (m_val2->type == JsonType::Number)
                                    f_max = m_val2->num_val;
                            }
                            std::string f_desc = f_val.get_string("description");
                            if (!f_name.empty()) {
                                td.add_field(StructField(f_name, f_type, f_def, f_unit, f_min, f_max, f_desc));
                            }
                        }
                    }
                }
                if (!td.name.empty()) {
                    out_ir.add_type(std::move(td));
                }
            }
        }
    }

    // Properties
    const auto* props_arr = root.get_child("properties");
    if (props_arr != nullptr && props_arr->is_array()) {
        for (const auto& p_val : props_arr->arr_val) {
            if (p_val.is_object()) {
                std::string p_id = p_val.get_string("id");
                std::string p_name = p_val.get_string("name");
                std::string formula =
                    p_val.get_string("raw_formula", p_val.get_string("ltl", p_val.get_string("formula")));
                std::string kind_str = p_val.get_string("kind", "Safety");
                PropertyKind p_kind = property_kind_from_string(kind_str);
                std::string req = p_val.get_string("req", p_val.get_string("traceability_req"));
                std::string desc = p_val.get_string("desc", p_val.get_string("description"));
                FormalProperty prop(p_name, p_kind, formula, desc, req);
                if (!p_id.empty()) {
                    prop.id = p_id;
                }
                if (!formula.empty()) {
                    prop.ast = PropertyAstNode::parse(formula);
                }
                out_ir.properties.push_back(std::move(prop));
            }
        }
    }

    // States
    const auto* states_obj = root.get_child("states");
    if (states_obj != nullptr && states_obj->is_object()) {
        parse_states_object(*states_obj, out_ir, "");
    } else if (states_obj != nullptr && states_obj->is_array()) {
        for (const auto& s_item : states_obj->arr_val) {
            if (s_item.is_object()) {
                std::string s_name = s_item.get_string("name", s_item.get_string("id"));
                std::string p_name =
                    sanitize_identifier(s_item.get_string("parent_state", s_item.get_string("parent")));
                if (!s_name.empty()) {
                    parse_single_state_entry(s_name, s_item, out_ir, p_name);
                }
            }
        }
    } else {
        out_error = "JSON Deserializer: Missing or invalid 'states' in JSON.";
        return false;
    }

    // Top-level Transitions list if present (only if transitions were not already parsed from states.on,
    // or for join transitions with empty source that do not appear in states.on)
    const bool had_on_transitions = !out_ir.transitions.empty();
    const auto* trans_arr = root.get_child("transitions");
    if (trans_arr != nullptr && trans_arr->is_array()) {
        for (const auto& t_val : trans_arr->arr_val) {
            if (t_val.is_object()) {
                std::string src = sanitize_identifier(t_val.get_string("source"));
                if (had_on_transitions && !src.empty()) {
                    continue;
                }
                std::string evt = t_val.get_string("event");
                if (evt.empty()) {
                    evt = t_val.get_string("trigger");
                }
                parse_single_transition_object(t_val, src, evt, out_ir);
            }
        }
    }

    if (out_ir.states.empty()) {
        out_error = "JSON Deserializer: No states extracted from JSON.";
        return false;
    }

    if (out_ir.initial_state.empty() && !out_ir.states.empty()) {
        out_ir.initial_state = out_ir.states.front().name;
    }

    for (auto& edge : out_ir.transitions) {
        if (!edge.source.empty()) {
            if (const auto* src_node = out_ir.find_state_by_name(edge.source)) {
                edge.source_id = src_node->id;
            }
        }
        if (!edge.target.empty()) {
            if (const auto* dst_node = out_ir.find_state_by_name(edge.target)) {
                edge.target_id = dst_node->id;
            }
        }
    }

    return true;
}

}  // namespace fsm::ir
