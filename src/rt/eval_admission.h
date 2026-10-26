#pragma once
// CP19 S1: bound newly exposed eval presentation without changing CP14 natives.
#include "rt/compiler.h"
#include "rt/native.h"
#include "rt/natives/builtin.h"
#include "rt/opcodes.h"

namespace nift::rt {

inline void refuse_eval_error_presentation(CompileResult& compiled) {
    if (!compiled.program) return;
    bool error_value = false;
    for (const auto& proto : compiled.program->protos)
        for (const auto& in : proto->code)
            error_value = error_value || in.op == OpCode::ErrorNew;
    if (!error_value) return;
    // Lambda/statement/import units are excluded by the eval entry. ErrorNew
    // is therefore the only producer of Error values in admitted expressions.
    // Co-occurrence is conservative: no claim of aggregate contents/aliasing.
    // Direct/nullable Error presentation uses ErrorJson and stays admitted.
    for (const auto& proto : compiled.program->protos)
        for (std::size_t pc = 0; pc < proto->code.size(); ++pc) {
            const auto& in = proto->code[pc];
            if (in.op != OpCode::CallNative || in.b < kFirstBuiltin) continue;
            const auto id = static_cast<natives::BuiltinId>(in.b - kFirstBuiltin);
            if (id != natives::BuiltinId::Stringify && id != natives::BuiltinId::Prettify &&
                id != natives::BuiltinId::Highlight) continue;
            compiled.reason = "eval:error-aggregate-presentation";
            compiled.offset = pc < proto->offsets.size() ? proto->offsets[pc] : 0;
            compiled.program.reset();
            return;
        }
}

} // namespace nift::rt
