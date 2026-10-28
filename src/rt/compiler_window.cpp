#include "rt/compiler_window.h"
// rt-lint: cold-file contiguous call operands, evaluated exactly once in source order
namespace nift::rt::detail {
bool UnitCompiler::operand_window(std::uint16_t base, std::uint32_t count) {
    const auto end = static_cast<std::uint32_t>(base) + (count ? count : 1u);
    while (fb_->top() < end) { std::uint16_t reg = 0; if (!temp(reg)) return false; }
    return true;
}
bool UnitCompiler::window_value(NodeId value, std::uint16_t dst) {
    const auto mark = fb_->top();
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 23
    // M23: adjacent operands overlap; CP22 made the former reserve-only
    // mutation equivalent, so it is retired rather than counted as killed.
    if (!expr_to(value, static_cast<std::uint16_t>(dst + 1u))) return false;
#else
    if (!expr_to(value, dst)) return false;
#endif
    fb_->release_to(mark); return true;
}
}
