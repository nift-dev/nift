#pragma once
// State shared by the default `for` compiler passes (CP12C slice 2).
#include "rt/compiler_expr.h"

namespace nift::rt::detail {

struct UnitCompiler::ForState {
    const Node* node = nullptr;
    const Node* header = nullptr;
    const Node* body = nullptr;
    bool pair = false;
    const NodeId* names = nullptr;
    const Binding* place = nullptr;
    Binding source_holder;
    std::string_view source_name;
    bool live = false;
    bool snapshot_mutant = false;
    bool saved_textual = false;
    bool error_only = false;
    int mode = 0;
    int force_mode = -1;
    std::uint16_t entry = 0;
    std::uint16_t held = 0;
    std::uint16_t count = 0;
    std::uint16_t keys = 0;
    std::uint16_t index = 0;
    std::uint16_t untyped = 0;
    bool entry_temp = false;
    Binding first_binding;
    Binding second_binding;
    Binding loop_binding;
    bool has_loop = false;
    bool reads_first = false;
    bool reads_second = false;
    std::uint32_t first_read = 0;
    std::uint32_t second_read = 0;
    bool first_used = false;
    bool second_used = false;
    bool track_missing = false;
    std::uint16_t first_missing = 0;
    std::uint16_t second_missing = 0;
    std::uint16_t first_flag = 0;
    std::uint16_t second_flag = 0;
    std::size_t loop_start = 0;
    std::size_t body_start = 0;
    std::vector<std::size_t> exits;
};

} // namespace nift::rt::detail
