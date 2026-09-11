#include "fsm/frontend/formal/sysml2_symbol_resolver.hpp"

#include <cctype>

namespace fsm::frontend::formal {

Sysml2SymbolResolver::Sysml2SymbolResolver() {
    init_standard_library();
}

void Sysml2SymbolResolver::add_search_path(std::string path) {
    search_paths_.push_back(std::move(path));
}

const std::vector<std::string>& Sysml2SymbolResolver::search_paths() const noexcept {
    return search_paths_;
}

void Sysml2SymbolResolver::register_import(std::string_view import_stmt) {
    std::string s(import_stmt);
    // e.g. "import ScalarValues::*" or "import MyPkg::MyType"
    if (s.rfind("import ", 0) == 0) {
        s = s.substr(7);
    }
    while (!s.empty() && (s.back() == ';' || std::isspace(static_cast<unsigned char>(s.back())) != 0)) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        start++;
    }
    std::string imp = s.substr(start);
    if (!imp.empty()) {
        imported_packages_.insert(imp);
    }
}

bool Sysml2SymbolResolver::is_standard_type(std::string_view type_name) const noexcept {
    return standard_types_.count(std::string(type_name)) != 0;
}

std::string Sysml2SymbolResolver::resolve_type(std::string_view raw_type) const {
    std::string t(raw_type);
    // Strip package qualifiers if imported
    if (t.rfind("ScalarValues::", 0) == 0) {
        t = t.substr(14);
    }
    auto it = type_map_.find(t);
    if (it != type_map_.end()) {
        return it->second;
    }
    return t;
}

void Sysml2SymbolResolver::init_standard_library() {
    // ScalarValues::*
    standard_types_ = {"Boolean",   "Integer", "Real",  "String", "Natural", "Positive",
                       "Numerical", "Complex", "Float", "Double", "Int32",   "uint32",
                       "int32",     "bool",    "int",   "float",  "double",  "string"};
    type_map_ = {{"Boolean", "bool"},      {"bool", "bool"},          {"Integer", "uint32_t"},  {"int", "uint32_t"},
                 {"uint32", "uint32_t"},   {"Int32", "int32_t"},      {"int32", "int32_t"},     {"Natural", "uint32_t"},
                 {"Positive", "uint32_t"}, {"Real", "float"},         {"float", "float"},       {"Double", "double"},
                 {"double", "double"},     {"String", "std::string"}, {"string", "std::string"}};
    imported_packages_.insert("ScalarValues::*");
    imported_packages_.insert("SI::*");
    imported_packages_.insert("ISQ::*");
}

}  // namespace fsm::frontend::formal
