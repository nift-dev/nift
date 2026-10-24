#include "rt/vm_frames.h"

// rt-lint: cold-file error reporting, once per stop

namespace nift::rt {

// CP12b modules: where the top-level code was when the program stopped (the
// module that was initialising, for legacy's `import: ` prefixes).
std::uint32_t Vm::main_offset(std::uint32_t own) const {
    if (frames_.empty()) return own;
    const Frame& main = frames_.front();
    return main.proto->offsets[static_cast<std::size_t>(main.ret - 1 - main.proto->code.data())];
}

std::uint32_t Vm::selector_offset(std::uint32_t own) const {
    const FunctionObject* active = closure_;
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        if (!active || !active->selector_call_site) break;
        own = it->proto->offsets[static_cast<std::size_t>(it->ret - 1 - it->proto->code.data())];
        active = it->closure;
    }
    return own;
}

} // namespace nift::rt
