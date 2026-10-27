#include "rt/compiler_blocks.h"

// rt-lint: cold-file compile-time block register ranges, once per unit

#include <vector>

namespace nift::rt::detail {

// CP17 unwind release: block register ranges (compiler_expr.h).
UnitCompiler::BlockRegs UnitCompiler::block_begin(NodeId block) {
    BlockRegs regs;
    regs.mark = fb_->top();
    regs.outer_high = fb_->reset_high();
    regs.clear = block_holds_handles(block);
    return regs;
}

void UnitCompiler::block_end(BlockRegs& regs) {
    regs.high = fb_->high();
    fb_->merge_high(regs.outer_high);
}

void UnitCompiler::block_clear(const BlockRegs& regs) {
    if (!regs.clear) return;
    for (std::uint16_t r = regs.mark; r < regs.high; ++r) fb_->emit(OpCode::LoadNull, r);
}

bool UnitCompiler::block_holds_handles(NodeId block) const {
    if (block == kNoNode) return false;
    std::vector<NodeId> stack{block};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (kinds(id) & kKindHandle) return true;
        const Node& n = ast_.at(id);
        if (n.kind == NodeKind::Lambda || n.kind == NodeKind::FnDecl) continue;  // their own frames
        for (NodeId child : {n.a, n.b, n.c})
            if (child != kNoNode) stack.push_back(child);
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k)
            if (list[k] != kNoNode) stack.push_back(list[k]);
    }
    return false;
}

} // namespace nift::rt::detail
