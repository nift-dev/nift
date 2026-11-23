#pragma once
// D-158 in-memory Minify++ adapter for the Runtime 2.0 native table.

#include "rt/natives/builtin.h"

namespace nift::rt::natives {
Status fn_minify(const Context&, const Value*, std::uint32_t, Value&, std::string&);
}
