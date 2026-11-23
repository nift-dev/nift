#pragma once

#include "rt/natives/builtin.h"

namespace nift::rt::natives {
Status fn_minify(const Context&, const Value*, std::uint32_t, Value&, std::string&);
}
