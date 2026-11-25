#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsm::ir {

enum class JsonType : std::uint8_t { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<JsonValue> arr_val;
    std::vector<std::string> obj_keys;
    std::vector<JsonValue> obj_values;
    std::unordered_map<std::string, std::size_t> obj_index;

    void set_member(std::string key, JsonValue val);
    [[nodiscard]] bool is_string() const { return type == JsonType::String; }
    [[nodiscard]] bool is_object() const { return type == JsonType::Object; }
    [[nodiscard]] bool is_array() const { return type == JsonType::Array; }
    [[nodiscard]] bool is_number() const { return type == JsonType::Number; }
    [[nodiscard]] std::string get_string(const std::string& key, const std::string& default_val = "") const;
    [[nodiscard]] double get_number(const std::string& key, double default_val = 0.0) const;
    [[nodiscard]] const JsonValue* get_child(const std::string& key) const;
};

class SimpleJsonParser {
  public:
    static bool parse(std::string_view input, JsonValue& out_root, std::string& err);
};

}  // namespace fsm::ir
