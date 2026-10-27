#include "rt/compiler_window.h"
// rt-lint: cold-file contiguous call operands, evaluated exactly once in source order
namespace nift::rt::detail {
bool UnitCompiler::operand_window(std::uint16_t base, std::uint32_t count) {
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 23
    (void)base; (void)count; return true;
#endif
    const auto end = static_cast<std::uint32_t>(base) + (count ? count : 1u);
    while (fb_->top() < end) { std::uint16_t reg = 0; if (!temp(reg)) return false; }
    return true;
}
bool UnitCompiler::window_value(NodeId value, std::uint16_t dst) {
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 23
    if (fb_->top() <= dst || dst + 1u < fb_->top()) if (!temp(dst)) return false;
    return expr_to(value, dst);
#else
    const auto mark = fb_->top();
    if (!expr_to(value, dst)) return false;
    fb_->release_to(mark); return true;
#endif
}
}
