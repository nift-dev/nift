#pragma once
// R1 (owner, 2026-10-21): a linked unit (one with static imports) is refused
// before it runs if any proto can call a host built-in, because the linked
// unit reads every module up front while legacy reads each at its import
// (host operations would see different files and trace in another order).
// One scan, shared by run_unit (rt/script_runner.cpp) and the refusal
// inventory (rt/refusal_inventory.cpp), so classify and classify-all agree.
#include "rt/native.h"
#include "rt/natives/builtin.h"
#include "rt/opcodes.h"
#include "rt/proto.h"

#include <cstdint>
#include <string>

namespace nift::rt {

// The first CallNative of a host built-in, in proto order then code order.
// Returns false (and leaves the outputs untouched) when there is none.
inline bool first_module_host_op(const Program& program, std::string& reason, std::uint32_t& offset) {
    for (const auto& proto : program.protos)
        for (std::size_t pc = 0; pc < proto->code.size(); ++pc) {
            const Instr& in = proto->code[pc];
            if (in.op != OpCode::CallNative || in.b < kFirstBuiltin) continue;
            const auto id = static_cast<natives::BuiltinId>(in.b - kFirstBuiltin);
            if (!natives::host_builtin(id)) continue;
            reason = "module-host-op:" + std::string(natives::builtin_spec(id).name);
            offset = pc < proto->offsets.size() ? proto->offsets[pc] : 0;
            return true;
        }
    return false;
}

} // namespace nift::rt
