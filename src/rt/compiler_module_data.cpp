#include "rt/compiler_module_data.h"

#include "rt/module_data.h"
#include "rt/string.h"

namespace nift::rt::detail {
int UnitCompiler::imported_data_call(const Node& node, std::uint16_t dst) {
    if (session_ || in_function_ || in_lambda_ || loop_reachable() || !imported_module_data(plan_, node)) return -1;
    auto literal = module_ascii_literal(ast_, node);
    if (!literal) return -1;
    const Node& callee = ast_.at(ast_.at(ast_.at(node.a).a).a);
    const auto name = text(callee);
    if (find_binding(name) || plan_.index.count(name) || plan_.struct_index.count(name))
        return fail(node, "builtin-bytes:decode");
    std::uint16_t constant;
    if (!fb_->constant(make_string(std::move(*literal)), constant)) return 0;
    fb_->set_offset(site(node));
    fb_->emit(OpCode::LoadK, dst, static_cast<std::uint16_t>(constant & ~kConstantBit));
    return builder_ok();
}
}
