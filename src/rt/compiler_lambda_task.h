#pragma once
// Lambda capture and task descriptors shared by the compiler and lambda lowering.
#include "rt/compiler.h"
#include "rt/ast.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace nift::rt::detail {

struct Capture {
    enum class Source : std::uint8_t { LocalCell, OuterUpvalue };
    Source source = Source::LocalCell;
    std::uint16_t index = 0;
    Binding binding;
};

struct LambdaTask {
    NodeId lambda = kNoNode;
    std::uint16_t proto_slot = 0;
    std::vector<std::string_view> upvalue_names;
    std::vector<NodeId> upvalue_decls;
    std::vector<bool> upvalue_loop;
    std::vector<bool> upvalue_readonly;
    std::vector<bool> upvalue_const_array;
    std::vector<Capture> captures;
    int owner_struct = -1;  // a lambda inside a struct method keeps `this`
};

} // namespace nift::rt::detail
