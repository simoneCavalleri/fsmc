/**
 * @file fuzz_json_deserializer.cpp
 * @brief libFuzzer harness exercising FsmIrDeserializer::deserialize_json and JSON roundtrip serialization.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_deserializer.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view input(reinterpret_cast<const char*>(data), size);
    fsm::ir::FsmIr ir;
    std::string err;
    if (fsm::ir::FsmIrDeserializer::deserialize_json(input, ir, err)) {
        (void)fsm::ir::FsmIrSerializer::serialize_json(ir);
    }
    return 0;
}
