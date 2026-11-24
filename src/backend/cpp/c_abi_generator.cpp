#include "fsm/backend/cpp/c_abi_generator.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "fsm/backend/cpp/cpp_backend_validator.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/ir/identifier_utils.hpp"

namespace fsm::backend::cpp {

using namespace fsm::ir;

namespace {

std::string to_snake_case(std::string_view raw) {
    std::string out;
    out.reserve(raw.size() + 8);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(raw[i]);
        if (std::isalnum(ch) != 0) {
            if (std::isupper(ch) != 0) {
                if (i > 0 && !out.empty() && out.back() != '_') {
                    const unsigned char prev = static_cast<unsigned char>(raw[i - 1]);
                    const bool prev_lower_or_digit = (std::islower(prev) != 0 || std::isdigit(prev) != 0);
                    const bool next_lower =
                        (i + 1 < raw.size() && std::islower(static_cast<unsigned char>(raw[i + 1])) != 0);
                    if (prev_lower_or_digit || next_lower) {
                        out.push_back('_');
                    }
                }
                out.push_back(static_cast<char>(std::tolower(ch)));
            } else {
                out.push_back(static_cast<char>(ch));
            }
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "fsm";
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::string to_upper_ascii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string sanitize_cpp_ident(std::string_view raw) {
    std::string res;
    res.reserve(raw.size() + 1);
    for (char c : raw) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) != 0 || c == '_') {
            res.push_back(c);
        } else {
            res.push_back('_');
        }
    }
    if (res.empty()) {
        res = "Fsm";
    }
    if (std::isdigit(static_cast<unsigned char>(res.front())) != 0) {
        res.insert(res.begin(), '_');
    }
    return res;
}

std::string sanitize_namespace(std::string_view raw_ns) {
    if (raw_ns.empty()) {
        return {};
    }
    std::string res;
    std::size_t pos = 0;
    while (pos < raw_ns.size()) {
        const std::size_t sep = raw_ns.find("::", pos);
        const std::string_view part =
            (sep == std::string_view::npos) ? raw_ns.substr(pos) : raw_ns.substr(pos, sep - pos);
        if (!part.empty()) {
            if (!res.empty()) {
                res += "::";
            }
            res += sanitize_cpp_ident(part);
        }
        if (sep == std::string_view::npos) {
            break;
        }
        pos = sep + 2;
    }
    return res;
}

struct CFieldMapping {
    std::string name;
    std::string c_type;
    bool is_enum_cast = false;
    bool is_local_struct = false;
    std::string struct_snake;
    std::string cpp_type;
};

struct CLocalStruct {
    std::string cpp_name;
    std::string snake_name;
    std::string c_type_name;
    std::vector<CFieldMapping> fields;
};

bool map_to_c_scalar(const DataType& dt, const FsmIr& model, std::string& out_c_type, bool& out_is_enum_cast,
                     std::string& out_cpp_type) {
    out_is_enum_cast = false;
    out_cpp_type = dt.to_cpp_type();
    if (dt.is_boolean()) {
        out_c_type = "bool";
        return true;
    }
    if (dt.is_integer()) {
        out_c_type = dt.to_cpp_type();
        return true;
    }
    if (dt.is_floating_point()) {
        out_c_type = dt.to_cpp_type();
        return true;
    }
    if (const auto* en = model.find_enum(dt.custom_name())) {
        out_c_type = "int32_t";
        out_is_enum_cast = true;
        out_cpp_type = sanitize_cpp_ident(en->name);
        return true;
    }
    return false;
}

bool map_to_c_type(const DataType& dt, const FsmIr& model, const std::vector<CLocalStruct>& local_structs,
                   CFieldMapping& out_fm) {
    out_fm.is_enum_cast = false;
    out_fm.is_local_struct = false;
    out_fm.struct_snake.clear();
    if (map_to_c_scalar(dt, model, out_fm.c_type, out_fm.is_enum_cast, out_fm.cpp_type)) {
        return true;
    }
    if (!dt.is_primitive()) {
        const std::string cname = sanitize_cpp_ident(dt.custom_name());
        for (const auto& ls : local_structs) {
            if (ls.cpp_name == cname) {
                out_fm.c_type = ls.c_type_name;
                out_fm.cpp_type = ls.cpp_name;
                out_fm.is_local_struct = true;
                out_fm.struct_snake = ls.snake_name;
                return true;
            }
        }
    }
    return false;
}

}  // namespace

GeneratedCAbiBundle CAbiGenerator::generate(const FsmIr& model, const GeneratorOptions& options,
                                            std::string_view fsm_header_include, std::string_view c_header_include) {
    FsmIr processed = model;
    processed.canonicalize();

    diagnostic::DiagnosticEngine diag;
    if (!CppBackendValidator::validate_model(processed, diag)) {
        throw std::invalid_argument(diag.render_to_string());
    }

    const std::string cpp_model_name = sanitize_cpp_ident(processed.name);
    const std::string prefix = to_snake_case(cpp_model_name);
    const std::string upper_prefix = to_upper_ascii(prefix);

    std::string effective_ns = options.target_namespace;
    if (effective_ns.empty()) {
        effective_ns = !processed.package.empty() ? processed.package : "fsm_generated";
    }
    effective_ns = sanitize_namespace(effective_ns);
    const std::string ns_qual = effective_ns.empty() ? "::" : ("::" + effective_ns + "::");

    const std::string fsm_inc = fsm_header_include.empty() ? (prefix + "_fsm.hpp") : std::string(fsm_header_include);
    const std::string c_inc = c_header_include.empty() ? (prefix + "_c.h") : std::string(c_header_include);

    // Collect local POD structs whose fields are C99-mappable (fixed-point for nested structs)
    std::vector<CLocalStruct> local_structs;
    {
        bool progress = true;
        std::unordered_set<std::string> added;
        while (progress) {
            progress = false;
            for (const auto& td : processed.custom_types) {
                if (td.kind != TypeKind::Struct || td.is_external()) {
                    continue;
                }
                const std::string cpp_name = sanitize_cpp_ident(td.name);
                if (added.count(cpp_name) != 0U) {
                    continue;
                }
                CLocalStruct ls;
                ls.cpp_name = cpp_name;
                ls.snake_name = to_snake_case(cpp_name);
                ls.c_type_name = prefix + "_" + ls.snake_name + "_t";
                bool all_ok = true;
                for (const auto& f : td.fields) {
                    CFieldMapping fm;
                    fm.name = sanitize_cpp_ident(f.name);
                    if (!map_to_c_type(f.type, processed, local_structs, fm)) {
                        all_ok = false;
                        break;
                    }
                    ls.fields.push_back(std::move(fm));
                }
                if (all_ok) {
                    added.insert(cpp_name);
                    local_structs.push_back(std::move(ls));
                    progress = true;
                }
            }
        }
    }

    // Collect C99-compatible scalar/enum/POD-struct fields for InPorts and OutPorts
    std::vector<CFieldMapping> in_fields;
    std::vector<CFieldMapping> out_fields;
    for (const auto& port : processed.ports) {
        CFieldMapping fm;
        fm.name = sanitize_cpp_ident(port.name);
        if (map_to_c_type(port.type, processed, local_structs, fm)) {
            if (port.is_in()) {
                in_fields.push_back(fm);
            }
            if (port.is_out()) {
                out_fields.push_back(fm);
            }
        }
    }

    // Collect non-internal events
    std::vector<SignalDefinition> events;
    for (const auto& sig : processed.signals) {
        if (sig.name == "anonymous_event" || sig.name == "completion_event") {
            continue;
        }
        events.push_back(sig);
    }

    // Conservative static opaque storage size (verified by static_assert in *_c.cpp)
    const std::size_t storage_bytes =
        std::max<std::size_t>(512U, 256U + (processed.variables.size() * 64U) + (processed.states.size() * 32U));

    const std::string ir_sha256 = FsmIrSerializer::compute_sha256(processed);

    // ========================================================================
    // 1. Pure C99 Header (*_c.h)
    // ========================================================================
    std::ostringstream hdr;
    hdr << "/**\n";
    hdr << " * @file " << c_inc << "\n";
    hdr << " * @brief Pure C99 zero-allocation ABI for '" << processed.name << "'\n";
    hdr << " * @note Generated by fsmc (--emit-c-abi). Zero dynamic memory allocation.\n";
    hdr << " * @note IR-SHA256: " << ir_sha256 << "\n";
    hdr << " */\n\n";
    hdr << "#ifndef " << upper_prefix << "_C_H\n";
    hdr << "#define " << upper_prefix << "_C_H\n\n";
    hdr << "#define " << upper_prefix << "_IR_SHA256 \"" << ir_sha256 << "\"\n\n";
    hdr << "#include <stdbool.h>\n";
    hdr << "#include <stddef.h>\n";
    hdr << "#include <stdint.h>\n\n";
    hdr << "#ifdef __cplusplus\n";
    hdr << "extern \"C\" {\n";
    hdr << "#endif\n\n";

    // State enum
    hdr << "typedef enum {\n";
    for (std::size_t i = 0; i < processed.states.size(); ++i) {
        hdr << "    " << upper_prefix << "_STATE_" << to_upper_ascii(to_snake_case(processed.states[i].name)) << " = "
            << i << ",\n";
    }
    hdr << "    " << upper_prefix << "_STATE_COUNT = " << processed.states.size() << "\n";
    hdr << "} " << prefix << "_state_t;\n\n";

    // Dispatch status enum
    hdr << "typedef enum {\n";
    hdr << "    " << upper_prefix << "_STATUS_OK = 0,\n";
    hdr << "    " << upper_prefix << "_STATUS_GUARD_REJECTED = 1,\n";
    hdr << "    " << upper_prefix << "_STATUS_IGNORED = 2,\n";
    hdr << "    " << upper_prefix << "_STATUS_INVALID_ARG = 3\n";
    hdr << "} " << prefix << "_status_t;\n\n";

    // Local POD structs
    for (const auto& ls : local_structs) {
        hdr << "typedef struct {\n";
        if (ls.fields.empty()) {
            hdr << "    uint8_t _reserved;\n";
        } else {
            for (const auto& f : ls.fields) {
                hdr << "    " << f.c_type << " " << f.name << ";\n";
            }
        }
        hdr << "} " << ls.c_type_name << ";\n\n";
    }

    // InPorts POD
    hdr << "typedef struct {\n";
    if (in_fields.empty()) {
        hdr << "    uint8_t _reserved;\n";
    } else {
        for (const auto& f : in_fields) {
            hdr << "    " << f.c_type << " " << f.name << ";\n";
        }
    }
    hdr << "} " << prefix << "_in_ports_t;\n\n";

    // OutPorts POD
    hdr << "typedef struct {\n";
    if (out_fields.empty()) {
        hdr << "    uint8_t _reserved;\n";
    } else {
        for (const auto& f : out_fields) {
            hdr << "    " << f.c_type << " " << f.name << ";\n";
        }
    }
    hdr << "} " << prefix << "_out_ports_t;\n\n";

    // Static aligned opaque storage (zero heap allocation, strict C99 portable alignment)
    const bool has_typed_services = !processed.services.empty();
    const std::size_t effective_storage_bytes =
        has_typed_services ? (storage_bytes + (processed.services.size() * 16U) + 64U) : storage_bytes;

    if (has_typed_services) {
        hdr << "typedef struct {\n";
        hdr << "    void* user_data;\n";
        for (const auto& svc : processed.services) {
            std::string ret_c = "void";
            if (!svc.is_void_return()) {
                bool is_enum_cast = false;
                std::string cpp_type;
                if (!map_to_c_scalar(svc.return_type, processed, ret_c, is_enum_cast, cpp_type)) {
                    ret_c = "int32_t";
                }
            }
            hdr << "    " << ret_c << " (*" << sanitize_cpp_ident(svc.name) << ")(void* user_data";
            for (const auto& param : svc.parameters) {
                std::string p_c = "int32_t";
                bool is_enum_cast = false;
                std::string cpp_type;
                map_to_c_scalar(param.type, processed, p_c, is_enum_cast, cpp_type);
                hdr << ", " << p_c << " " << sanitize_cpp_ident(param.name);
            }
            hdr << ");\n";
        }
        hdr << "} " << prefix << "_services_t;\n\n";
    }

    hdr << "enum { " << upper_prefix << "_STORAGE_SIZE = " << effective_storage_bytes << " };\n\n";
    hdr << "typedef struct {\n";
    hdr << "    union {\n";
    hdr << "        long double _align_ld;\n";
    hdr << "        long long _align_ll;\n";
    hdr << "        void* _align_ptr;\n";
    hdr << "        uint8_t _bytes[" << upper_prefix << "_STORAGE_SIZE];\n";
    hdr << "    } _storage;\n";
    hdr << "} " << prefix << "_fsm_t;\n\n";

    // Lifecycle, query, sampled step/tick, and snapshot API
    hdr << "void " << prefix << "_init(" << prefix << "_fsm_t* fsm);\n";
    if (has_typed_services) {
        hdr << "void " << prefix << "_init_with_services(" << prefix << "_fsm_t* fsm, const " << prefix
            << "_services_t* services);\n";
    }
    hdr << "void " << prefix << "_destroy(" << prefix << "_fsm_t* fsm);\n";
    hdr << prefix << "_state_t " << prefix << "_current_state(const " << prefix << "_fsm_t* fsm);\n";
    hdr << "const char* " << prefix << "_state_name(" << prefix << "_state_t state);\n";
    hdr << "const char* " << prefix << "_ir_sha256(void);\n";
    hdr << prefix << "_status_t " << prefix << "_step(" << prefix << "_fsm_t* fsm, const " << prefix
        << "_in_ports_t* in_ports, " << prefix << "_out_ports_t* out_ports);\n";
    hdr << prefix << "_status_t " << prefix << "_tick(" << prefix << "_fsm_t* fsm, uint64_t delta_ms, const " << prefix
        << "_in_ports_t* in_ports, " << prefix << "_out_ports_t* out_ports);\n";
    hdr << "size_t " << prefix << "_snapshot_size(const " << prefix << "_fsm_t* fsm);\n";
    hdr << "bool " << prefix << "_save_snapshot(const " << prefix
        << "_fsm_t* fsm, uint8_t* out_buf, size_t capacity, size_t* bytes_written);\n";
    hdr << "bool " << prefix << "_restore_snapshot(" << prefix
        << "_fsm_t* fsm, const uint8_t* buf, size_t size, size_t* bytes_read);\n\n";

    // Event dispatchers
    for (const auto& sig : events) {
        const std::string ev_snake = to_snake_case(sig.name);
        hdr << prefix << "_status_t " << prefix << "_dispatch_" << ev_snake << "(" << prefix << "_fsm_t* fsm";
        for (const auto& attr : sig.attributes) {
            CFieldMapping fm;
            fm.name = sanitize_cpp_ident(attr.name);
            if (map_to_c_type(attr.type, processed, local_structs, fm)) {
                if (fm.is_local_struct) {
                    hdr << ", const " << fm.c_type << "* " << fm.name;
                } else {
                    hdr << ", " << fm.c_type << " " << fm.name;
                }
            }
        }
        hdr << ", const " << prefix << "_in_ports_t* in_ports, " << prefix << "_out_ports_t* out_ports);\n";
    }

    hdr << "\n#ifdef __cplusplus\n";
    hdr << "} /* extern \"C\" */\n";
    hdr << "#endif\n\n";
    hdr << "#endif /* " << upper_prefix << "_C_H */\n";

    // ========================================================================
    // 2. Zero-Allocation C++17 Bridge (*_c.cpp)
    // ========================================================================
    std::ostringstream src;
    src << "/**\n";
    src << " * @file " << prefix << "_c.cpp\n";
    src << " * @brief Zero-allocation C++17 bridge implementing " << c_inc << "\n";
    src << " * @note Generated by fsmc (--emit-c-abi). Uses placement-new only; zero heap allocations.\n";
    src << " */\n\n";
    src << "#include \"" << c_inc << "\"\n";
    src << "#include \"" << fsm_inc << "\"\n\n";
    src << "#include <new>\n\n";

    src << "namespace {\n\n";
    src << "using CppInPorts = " << ns_qual << cpp_model_name << "InPorts;\n";
    src << "using CppOutPorts = " << ns_qual << cpp_model_name << "OutPorts;\n";
    src << "using CppRegisters = " << ns_qual << cpp_model_name << "Registers;\n\n";

    if (has_typed_services) {
        src << "struct CServicesBridge {\n";
        src << "    " << prefix << "_services_t cb{};\n";
        src << "    explicit CServicesBridge(const " << prefix << "_services_t* services) noexcept {\n";
        src << "        if (services != nullptr) {\n";
        src << "            cb = *services;\n";
        src << "        }\n";
        src << "    }\n";
        for (const auto& svc : processed.services) {
            const std::string m_name = sanitize_cpp_ident(svc.name);
            const std::string ret_cpp = svc.is_void_return() ? "void" : svc.return_type.to_cpp_type();
            src << "    " << ret_cpp << " " << m_name << "(";
            for (std::size_t pi = 0; pi < svc.parameters.size(); ++pi) {
                if (pi > 0) {
                    src << ", ";
                }
                src << svc.parameters[pi].type.to_cpp_type() << " " << sanitize_cpp_ident(svc.parameters[pi].name);
            }
            src << ")";
            if (svc.is_noexcept) {
                src << " noexcept";
            }
            src << " {\n";
            src << "        if (cb." << m_name << " != nullptr) {\n";
            src << "            ";
            if (!svc.is_void_return()) {
                src << "return static_cast<" << ret_cpp << ">(";
            }
            src << "cb." << m_name << "(cb.user_data";
            for (const auto& param : svc.parameters) {
                std::string p_c = "int32_t";
                bool is_enum_cast = false;
                std::string cpp_type;
                map_to_c_scalar(param.type, processed, p_c, is_enum_cast, cpp_type);
                src << ", static_cast<" << p_c << ">(" << sanitize_cpp_ident(param.name) << ")";
            }
            if (!svc.is_void_return()) {
                src << ")";
            }
            src << ");\n";
            src << "        }\n";
            if (!svc.is_void_return()) {
                src << "        return " << ret_cpp << "{};\n";
            }
            src << "    }\n";
        }
        src << "};\n\n";
        src << "using CppFsm = " << ns_qual << cpp_model_name << "WithServices<CServicesBridge>;\n\n";
        src << "struct CppInstance {\n";
        src << "    CppRegisters reg{};\n";
        src << "    CServicesBridge srv;\n";
        src << "    CppFsm fsm;\n";
        src << "    explicit CppInstance(const " << prefix
            << "_services_t* services) : srv(services), fsm(reg, srv) {}\n";
        src << "};\n\n";
        src << "static_assert(sizeof(CppInstance) <= sizeof(" << prefix << "_fsm_t),\n";
        src << "              \"" << prefix << "_fsm_t opaque storage is smaller than " << cpp_model_name << "\");\n";
        src << "static_assert(alignof(CppInstance) <= alignof(" << prefix << "_fsm_t),\n";
        src << "              \"" << prefix << "_fsm_t alignment is weaker than " << cpp_model_name << "\");\n\n";
        src << "CppInstance* as_inst(" << prefix << "_fsm_t* fsm) noexcept {\n";
        src << "    return std::launder(reinterpret_cast<CppInstance*>(fsm->_storage._bytes));\n";
        src << "}\n\n";
        src << "const CppInstance* as_inst(const " << prefix << "_fsm_t* fsm) noexcept {\n";
        src << "    return std::launder(reinterpret_cast<const CppInstance*>(fsm->_storage._bytes));\n";
        src << "}\n\n";
        src << "CppFsm* as_cpp(" << prefix << "_fsm_t* fsm) noexcept { return &as_inst(fsm)->fsm; }\n";
        src << "const CppFsm* as_cpp(const " << prefix << "_fsm_t* fsm) noexcept { return &as_inst(fsm)->fsm; }\n\n";
    } else {
        src << "using CppFsm = " << ns_qual << cpp_model_name << ";\n\n";
        src << "static_assert(sizeof(CppFsm) <= sizeof(" << prefix << "_fsm_t),\n";
        src << "              \"" << prefix << "_fsm_t opaque storage is smaller than " << cpp_model_name << "\");\n";
        src << "static_assert(alignof(CppFsm) <= alignof(" << prefix << "_fsm_t),\n";
        src << "              \"" << prefix << "_fsm_t alignment is weaker than " << cpp_model_name << "\");\n\n";
        src << "CppFsm* as_cpp(" << prefix << "_fsm_t* fsm) noexcept {\n";
        src << "    return std::launder(reinterpret_cast<CppFsm*>(fsm->_storage._bytes));\n";
        src << "}\n\n";
        src << "const CppFsm* as_cpp(const " << prefix << "_fsm_t* fsm) noexcept {\n";
        src << "    return std::launder(reinterpret_cast<const CppFsm*>(fsm->_storage._bytes));\n";
        src << "}\n\n";
    }

    src << "[[maybe_unused]] " << prefix << "_status_t map_status(::fsm::dispatch_status s) noexcept {\n";
    src << "    switch (s) {\n";
    src << "        case ::fsm::dispatch_status::success:\n";
    src << "        case ::fsm::dispatch_status::deferred:\n";
    src << "            return " << upper_prefix << "_STATUS_OK;\n";
    src << "        case ::fsm::dispatch_status::guard_rejected:\n";
    src << "            return " << upper_prefix << "_STATUS_GUARD_REJECTED;\n";
    src << "        default:\n";
    src << "            return " << upper_prefix << "_STATUS_IGNORED;\n";
    src << "    }\n";
    src << "}\n\n";

    src << prefix << "_status_t map_step_status(::fsm::step_status s) noexcept {\n";
    src << "    return (s == ::fsm::step_status::transitioned) ? " << upper_prefix << "_STATUS_OK : " << upper_prefix
        << "_STATUS_IGNORED;\n";
    src << "}\n\n";

    // Local POD struct converters
    for (const auto& ls : local_structs) {
        src << "[[maybe_unused]] " << ns_qual << ls.cpp_name << " to_cpp_" << ls.snake_name << "(const "
            << ls.c_type_name << "& c_val) noexcept {\n";
        src << "    " << ns_qual << ls.cpp_name << " res{};\n";
        if (ls.fields.empty()) {
            src << "    (void)c_val;\n";
        } else {
            for (const auto& f : ls.fields) {
                if (f.is_enum_cast) {
                    src << "    res." << f.name << " = static_cast<" << ns_qual << f.cpp_type << ">(c_val." << f.name
                        << ");\n";
                } else if (f.is_local_struct) {
                    src << "    res." << f.name << " = to_cpp_" << f.struct_snake << "(c_val." << f.name << ");\n";
                } else {
                    src << "    res." << f.name << " = c_val." << f.name << ";\n";
                }
            }
        }
        src << "    return res;\n";
        src << "}\n\n";

        src << "[[maybe_unused]] " << ls.c_type_name << " from_cpp_" << ls.snake_name << "(const " << ns_qual
            << ls.cpp_name << "& cpp_val) noexcept {\n";
        src << "    " << ls.c_type_name << " res{};\n";
        if (ls.fields.empty()) {
            src << "    (void)cpp_val;\n";
        } else {
            for (const auto& f : ls.fields) {
                if (f.is_enum_cast) {
                    src << "    res." << f.name << " = static_cast<int32_t>(cpp_val." << f.name << ");\n";
                } else if (f.is_local_struct) {
                    src << "    res." << f.name << " = from_cpp_" << f.struct_snake << "(cpp_val." << f.name << ");\n";
                } else {
                    src << "    res." << f.name << " = cpp_val." << f.name << ";\n";
                }
            }
        }
        src << "    return res;\n";
        src << "}\n\n";
    }

    src << "CppInPorts to_cpp_in(const " << prefix << "_in_ports_t* in_ports) noexcept {\n";
    src << "    CppInPorts res{};\n";
    if (!in_fields.empty()) {
        src << "    if (in_ports != nullptr) {\n";
        for (const auto& f : in_fields) {
            if (f.is_enum_cast) {
                src << "        res." << f.name << " = static_cast<" << ns_qual << f.cpp_type << ">(in_ports->"
                    << f.name << ");\n";
            } else if (f.is_local_struct) {
                src << "        res." << f.name << " = to_cpp_" << f.struct_snake << "(in_ports->" << f.name << ");\n";
            } else {
                src << "        res." << f.name << " = in_ports->" << f.name << ";\n";
            }
        }
        src << "    }\n";
    } else {
        src << "    (void)in_ports;\n";
    }
    src << "    return res;\n";
    src << "}\n\n";

    src << "void from_cpp_out(const CppOutPorts& cpp_out, " << prefix << "_out_ports_t* out_ports) noexcept {\n";
    if (!out_fields.empty()) {
        src << "    if (out_ports == nullptr) {\n";
        src << "        return;\n";
        src << "    }\n";
        for (const auto& f : out_fields) {
            if (f.is_enum_cast) {
                src << "    out_ports->" << f.name << " = static_cast<int32_t>(cpp_out." << f.name << ");\n";
            } else if (f.is_local_struct) {
                src << "    out_ports->" << f.name << " = from_cpp_" << f.struct_snake << "(cpp_out." << f.name
                    << ");\n";
            } else {
                src << "    out_ports->" << f.name << " = cpp_out." << f.name << ";\n";
            }
        }
    } else {
        src << "    (void)cpp_out;\n";
        src << "    (void)out_ports;\n";
    }
    src << "}\n\n";
    src << "}  // namespace\n\n";

    src << "extern \"C\" {\n\n";

    // init / destroy
    if (has_typed_services) {
        src << "void " << prefix << "_init_with_services(" << prefix << "_fsm_t* fsm, const " << prefix
            << "_services_t* services) {\n";
        src << "    if (fsm == nullptr) {\n";
        src << "        return;\n";
        src << "    }\n";
        src << "    ::new (static_cast<void*>(fsm->_storage._bytes)) CppInstance(services);\n";
        src << "}\n\n";
        src << "void " << prefix << "_init(" << prefix << "_fsm_t* fsm) {\n";
        src << "    " << prefix << "_init_with_services(fsm, nullptr);\n";
        src << "}\n\n";
        src << "void " << prefix << "_destroy(" << prefix << "_fsm_t* fsm) {\n";
        src << "    if (fsm == nullptr) {\n";
        src << "        return;\n";
        src << "    }\n";
        src << "    as_inst(fsm)->~CppInstance();\n";
        src << "}\n\n";
    } else {
        src << "void " << prefix << "_init(" << prefix << "_fsm_t* fsm) {\n";
        src << "    if (fsm == nullptr) {\n";
        src << "        return;\n";
        src << "    }\n";
        src << "    ::new (static_cast<void*>(fsm->_storage._bytes)) CppFsm();\n";
        src << "}\n\n";
        src << "void " << prefix << "_destroy(" << prefix << "_fsm_t* fsm) {\n";
        src << "    if (fsm == nullptr) {\n";
        src << "        return;\n";
        src << "    }\n";
        src << "    as_cpp(fsm)->~CppFsm();\n";
        src << "}\n\n";
    }

    src << "const char* " << prefix << "_ir_sha256(void) {\n";
    src << "    return " << upper_prefix << "_IR_SHA256;\n";
    src << "}\n\n";

    // current_state
    src << prefix << "_state_t " << prefix << "_current_state(const " << prefix << "_fsm_t* fsm) {\n";
    src << "    if (fsm == nullptr) {\n";
    src << "        return " << upper_prefix << "_STATE_COUNT;\n";
    src << "    }\n";
    src << "    const CppFsm* sm = as_cpp(fsm);\n";
    for (const auto& st : processed.states) {
        const std::string st_cpp = sanitize_cpp_ident(st.name);
        const std::string st_enum = upper_prefix + "_STATE_" + to_upper_ascii(to_snake_case(st.name));
        src << "    if (sm->is_in<" << ns_qual << st_cpp << ">()) {\n";
        src << "        return " << st_enum << ";\n";
        src << "    }\n";
    }
    src << "    return " << upper_prefix << "_STATE_COUNT;\n";
    src << "}\n\n";

    // state_name
    src << "const char* " << prefix << "_state_name(" << prefix << "_state_t state) {\n";
    src << "    switch (state) {\n";
    for (const auto& st : processed.states) {
        const std::string st_enum = upper_prefix + "_STATE_" + to_upper_ascii(to_snake_case(st.name));
        src << "        case " << st_enum << ":\n";
        src << "            return \"" << st.name << "\";\n";
    }
    src << "        default:\n";
    src << "            return \"unknown\";\n";
    src << "    }\n";
    src << "}\n\n";

    // Sampled step
    src << prefix << "_status_t " << prefix << "_step(" << prefix << "_fsm_t* fsm, const " << prefix
        << "_in_ports_t* in_ports, " << prefix << "_out_ports_t* out_ports) {\n";
    src << "    if (fsm == nullptr) {\n";
    src << "        return " << upper_prefix << "_STATUS_INVALID_ARG;\n";
    src << "    }\n";
    src << "    const CppInPorts cpp_in = to_cpp_in(in_ports);\n";
    src << "    CppOutPorts cpp_out{};\n";
    src << "    const auto res = as_cpp(fsm)->step(cpp_in, cpp_out);\n";
    src << "    from_cpp_out(cpp_out, out_ports);\n";
    src << "    return map_step_status(res.status);\n";
    src << "}\n\n";

    // Timed tick + sampled step
    src << prefix << "_status_t " << prefix << "_tick(" << prefix << "_fsm_t* fsm, uint64_t delta_ms, const " << prefix
        << "_in_ports_t* in_ports, " << prefix << "_out_ports_t* out_ports) {\n";
    src << "    if (fsm == nullptr) {\n";
    src << "        return " << upper_prefix << "_STATUS_INVALID_ARG;\n";
    src << "    }\n";
    src << "    const CppInPorts cpp_in = to_cpp_in(in_ports);\n";
    src << "    CppOutPorts cpp_out{};\n";
    src << "    const auto res = as_cpp(fsm)->step(delta_ms, cpp_in, cpp_out);\n";
    src << "    from_cpp_out(cpp_out, out_ports);\n";
    src << "    return map_step_status(res.status);\n";
    src << "}\n\n";

    // Snapshot size / save / restore
    src << "size_t " << prefix << "_snapshot_size(const " << prefix << "_fsm_t* fsm) {\n";
    src << "    if (fsm == nullptr) {\n";
    src << "        return 0U;\n";
    src << "    }\n";
    src << "    return as_cpp(fsm)->serialized_size();\n";
    src << "}\n\n";

    src << "bool " << prefix << "_save_snapshot(const " << prefix
        << "_fsm_t* fsm, uint8_t* out_buf, size_t capacity, size_t* bytes_written) {\n";
    src << "    if (fsm == nullptr || out_buf == nullptr) {\n";
    src << "        if (bytes_written != nullptr) {\n";
    src << "            *bytes_written = 0U;\n";
    src << "        }\n";
    src << "        return false;\n";
    src << "    }\n";
    src << "    std::size_t written = 0U;\n";
    src << "    const bool ok = as_cpp(fsm)->serialize(out_buf, capacity, written);\n";
    src << "    if (bytes_written != nullptr) {\n";
    src << "        *bytes_written = written;\n";
    src << "    }\n";
    src << "    return ok;\n";
    src << "}\n\n";

    src << "bool " << prefix << "_restore_snapshot(" << prefix
        << "_fsm_t* fsm, const uint8_t* buf, size_t size, size_t* bytes_read) {\n";
    src << "    if (fsm == nullptr || buf == nullptr) {\n";
    src << "        if (bytes_read != nullptr) {\n";
    src << "            *bytes_read = 0U;\n";
    src << "        }\n";
    src << "        return false;\n";
    src << "    }\n";
    src << "    std::size_t read_bytes = 0U;\n";
    src << "    const bool ok = as_cpp(fsm)->deserialize(buf, size, read_bytes);\n";
    src << "    if (bytes_read != nullptr) {\n";
    src << "        *bytes_read = read_bytes;\n";
    src << "    }\n";
    src << "    return ok;\n";
    src << "}\n\n";

    // Event dispatchers
    for (const auto& sig : events) {
        const std::string ev_snake = to_snake_case(sig.name);
        const std::string ev_cpp = sanitize_cpp_ident(sig.name);
        src << prefix << "_status_t " << prefix << "_dispatch_" << ev_snake << "(" << prefix << "_fsm_t* fsm";
        std::vector<CFieldMapping> ev_fields;
        for (const auto& attr : sig.attributes) {
            CFieldMapping fm;
            fm.name = sanitize_cpp_ident(attr.name);
            if (map_to_c_type(attr.type, processed, local_structs, fm)) {
                if (fm.is_local_struct) {
                    src << ", const " << fm.c_type << "* " << fm.name;
                } else {
                    src << ", " << fm.c_type << " " << fm.name;
                }
                ev_fields.push_back(fm);
            }
        }
        src << ", const " << prefix << "_in_ports_t* in_ports, " << prefix << "_out_ports_t* out_ports) {\n";
        src << "    if (fsm == nullptr) {\n";
        src << "        return " << upper_prefix << "_STATUS_INVALID_ARG;\n";
        src << "    }\n";
        src << "    " << ns_qual << ev_cpp << " ev{};\n";
        for (const auto& ef : ev_fields) {
            if (ef.is_enum_cast) {
                src << "    ev." << ef.name << " = static_cast<" << ns_qual << ef.cpp_type << ">(" << ef.name << ");\n";
            } else if (ef.is_local_struct) {
                src << "    if (" << ef.name << " != nullptr) {\n";
                src << "        ev." << ef.name << " = to_cpp_" << ef.struct_snake << "(*" << ef.name << ");\n";
                src << "    }\n";
            } else {
                src << "    ev." << ef.name << " = " << ef.name << ";\n";
            }
        }
        src << "    const CppInPorts cpp_in = to_cpp_in(in_ports);\n";
        src << "    CppOutPorts cpp_out{};\n";
        src << "    const auto res = as_cpp(fsm)->dispatch(ev, cpp_in, cpp_out);\n";
        src << "    from_cpp_out(cpp_out, out_ports);\n";
        src << "    return map_status(res.status);\n";
        src << "}\n\n";
    }

    src << "}  // extern \"C\"\n";

    return GeneratedCAbiBundle{hdr.str(), src.str()};
}

}  // namespace fsm::backend::cpp
