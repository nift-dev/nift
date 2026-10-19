#pragma once
// NIFT_HANDLE_LIMIT (D-035/D-028): the handle table capacity setting.
// NIFT_HANDLE_LIMIT=<count>[K|M] (binary multiples), 1 .. 4294967295. Unset or
// empty: the default (2^20). An invalid value is reported by every create()
// (InvalidSetting), never silently replaced.
#include "rt/handles/types.h"

#include <cstdint>
#include <string>

namespace nift::rt::handles {

struct HandleLimitSetting {
    std::uint32_t capacity = TableOptions().capacity;
    bool valid = true;
    std::string raw;   // the rejected text when !valid
};

HandleLimitSetting parse_handle_limit(const char* value);
// TableOptions from the environment (capacity or the rejected text).
TableOptions table_options_from_environment();

} // namespace nift::rt::handles
