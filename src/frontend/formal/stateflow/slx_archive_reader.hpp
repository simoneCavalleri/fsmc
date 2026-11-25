#pragma once

#include <string>
#include <string_view>

namespace fsm::frontend::formal::stateflow_detail {

bool extract_stateflow_from_slx(std::string_view slx_data, std::string& extracted_xml, std::string& err);

}  // namespace fsm::frontend::formal::stateflow_detail
