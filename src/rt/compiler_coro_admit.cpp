#include "rt/compiler_coro_admit.h"

#include "rt/compiler_coro_loops.h"
#include "rt/native.h"
#include "rt/natives/builtin.h"

#include <cstdlib>
#include <string_view>
#include <vector>

// rt-lint: cold-file compile-time cooperative admission, once per coroutine unit

// Test-only deliberate defects (`-DNIFT_CORO_MUTANT=n`, tests/runtime2/cp22):
// 12 the admission rule admits everything; 13 A5 off; 14 A1 off; 15 A2 off;
// 16 A3 off; 17 A4 off; 20 calls through values are not followed (the
// rejected round-5 member-call gap); 19, 21 and 22 weaken A3
// (compiler_coro_loops.cpp). Normal builds contain none.
#ifndef NIFT_CORO_MUTANT
#define NIFT_CORO_MUTANT 0
#endif

namespace nift::rt {

namespace {

using natives::BuiltinId;

// A1: Host built-ins that neither block nor wait on I/O (sleep suspends).
bool host_allowed(BuiltinId id) {
    switch (id) {
        case BuiltinId::HostSleep: case BuiltinId::HostEpoch: case BuiltinId::HostGetenv: case BuiltinId::HostOs:
        case BuiltinId::HostArch: case BuiltinId::HostPlatform: case BuiltinId::HostHardwareConcurrency:
            return true;
        default: return false;
    }
}

// A4: built-ins task code may call: constant-time reads and `type`.
bool task_builtin_allowed(BuiltinId id) {
    switch (id) {
        case BuiltinId::HostSleep: case BuiltinId::HostEpoch: case BuiltinId::HostGetenv: case BuiltinId::HostOs:
        case BuiltinId::HostArch: case BuiltinId::HostPlatform: case BuiltinId::HostHardwareConcurrency:
        case BuiltinId::FnType:
            return true;
        default: return false;
    }
}

// A4: instructions task code may execute. Arithmetic, comparisons and string
// concatenation are data-proportional operators (record §12.1.1 residual);
// everything that is not listed (collection callbacks, in-place collection
// mutation, JSON of Errors, struct and top-level instructions) is refused.
bool task_op_allowed(OpCode op) {
    switch (op) {
        case OpCode::Nop: case OpCode::LoadK: case OpCode::LoadI: case OpCode::LoadNull: case OpCode::LoadBool:
        case OpCode::Move: case OpCode::Add: case OpCode::Sub: case OpCode::Mul: case OpCode::Div: case OpCode::Mod:
        case OpCode::AddI: case OpCode::AddAssign: case OpCode::Eq: case OpCode::Ne: case OpCode::Lt: case OpCode::Le:
        case OpCode::Gt: case OpCode::Ge: case OpCode::Not: case OpCode::Truth: case OpCode::Neg: case OpCode::Pos:
        case OpCode::BindType: case OpCode::CheckType: case OpCode::CheckAssign: case OpCode::TypeMeet:
        case OpCode::Jmp: case OpCode::JmpFalse: case OpCode::JmpTrue: case OpCode::JmpNotNull: case OpCode::JmpNotLt:
        case OpCode::JmpNotLe: case OpCode::JmpNotGt: case OpCode::JmpNotGe: case OpCode::JmpNotEq:
        case OpCode::JmpNotNe: case OpCode::JmpIncLt: case OpCode::Call: case OpCode::CallNative:
        case OpCode::Return: case OpCode::ReturnNull: case OpCode::NewArray: case OpCode::NewObject:
        case OpCode::ObjectSet: case OpCode::GetIndex: case OpCode::SetIndex: case OpCode::GetMember:
        case OpCode::Push: case OpCode::Length: case OpCode::GetGlobal: case OpCode::GetGlobalChecked:
        case OpCode::SetGlobal: case OpCode::MarkGlobal: case OpCode::CheckGlobal: case OpCode::LoadFunction:
        case OpCode::NewCell: case OpCode::CellLoad: case OpCode::CellStore: case OpCode::Closure:
        case OpCode::CallValue: case OpCode::CallValueAny: case OpCode::Undefined: case OpCode::IterCheck:
        case OpCode::ArrayElem: case OpCode::ArrayElemValue: case OpCode::ObjectKeys: case OpCode::ObjectLookup:
        case OpCode::ObjectLookupValue: case OpCode::IterMissing: case OpCode::CopyCellLocation:
        case OpCode::CellLoadCached: case OpCode::CellStoreCached: case OpCode::CellTake: case OpCode::NewSourceSlot:
        case OpCode::CellAnchor: case OpCode::CellSlot: case OpCode::TryBegin: case OpCode::TryEnd:
        case OpCode::Throw: case OpCode::ErrorNew: case OpCode::Spawn: case OpCode::Await: case OpCode::Sleep:
        case OpCode::Yield: case OpCode::TaskEnd: case OpCode::FutureOp:
            return true;
        default: return false;
    }
}

std::string native_name(std::uint16_t b) {
    if (b < kFirstBuiltin) return "print";
    return std::string(natives::builtin_spec(static_cast<BuiltinId>(b - kFirstBuiltin)).name);
}

class Admission {
public:
    Admission(const Program& program, bool observe) : program_(program), observe_(observe) {}

    bool run(std::string& reason, std::uint32_t& offset) {
#if NIFT_CORO_MUTANT == 12
        return true;  // M12: cooperative admission disabled
#endif
#if NIFT_CORO_MUTANT == 13
        observe_ = true;  // M13: scheduling observations admitted (A5 off)
#endif
        const std::size_t n = program_.protos.size();
        for (std::size_t p = 0; p < n; ++p)
            if (!unit_wide(p, reason, offset)) return false;
        collect_tasks();
#if NIFT_CORO_MUTANT != 15
        if (cyclic(reason, offset)) return false;
#endif
        for (std::size_t p = 0; p < n; ++p)
            if (task_[p] && !task_code(p, reason, offset)) return false;
        return true;
    }

private:
    const Proto& proto(std::size_t p) const { return *program_.protos[p]; }
    bool fail(std::size_t p, std::size_t pc, std::string why, std::string& reason, std::uint32_t& offset) const {
        reason = std::move(why);
        offset = pc < proto(p).offsets.size() ? proto(p).offsets[pc] : 0;
        return false;
    }

    // A1 and the unit-wide part of A5 (every proto, main included).
    bool unit_wide(std::size_t p, std::string& reason, std::uint32_t& offset) const {
        const std::vector<Instr>& code = proto(p).code;
        for (std::size_t pc = 0; pc < code.size(); ++pc) {
            const Instr& in = code[pc];
            if (in.op == OpCode::FutureOp && !observe_)
                return fail(p, pc, in.c == 0 ? "coro-observe:done" : "coro-observe:status", reason, offset);
            if (in.op != OpCode::CallNative || in.b < kFirstBuiltin) continue;
            const auto id = static_cast<BuiltinId>(in.b - kFirstBuiltin);
            if (id == BuiltinId::HostEpoch && !observe_) return fail(p, pc, "coro-observe:epoch", reason, offset);
#if NIFT_CORO_MUTANT != 14
            if (natives::host_builtin(id) && !host_allowed(id))
                return fail(p, pc, "coro-blocking-host:" + native_name(in.b), reason, offset);
#endif
        }
        return true;
    }

    // Task code: async protos, then everything reachable by Call/Spawn; a call
    // through a value reaches every proto that is ever a function value.
    void collect_tasks() {
        const std::size_t n = program_.protos.size();
        task_.assign(n, false);
        value_call_.assign(n, false);
        edges_.assign(n, {});
        for (std::size_t p = 0; p < n; ++p) {
            const std::vector<Instr>& code = proto(p).code;
            for (std::size_t pc = 0; pc < code.size(); ++pc) {
                const Instr& in = code[pc];
                if (in.op == OpCode::LoadFunction || in.op == OpCode::Closure) values_.push_back(in.b);
                if (in.op == OpCode::Call || in.op == OpCode::Spawn) edges_[p].push_back(in.b);
                if (in.op != OpCode::CallValue && in.op != OpCode::CallValueAny) continue;
                const int target = (in.op == OpCode::CallValueAny && (in.c & 0x8000u)) ? thread_target(code, pc) : -1;
                if (target >= 0) edges_[p].push_back(static_cast<std::uint16_t>(target));
                else value_call_[p] = true;
            }
        }
        std::vector<std::size_t> work;
        for (std::size_t p = 1; p < n; ++p)
            if (proto(p).is_async) work.push_back(p);
        while (!work.empty()) {
            const std::size_t p = work.back();
            work.pop_back();
            if (p >= n || task_[p]) continue;
            task_[p] = true;
            for (std::uint16_t q : edges_[p]) work.push_back(q);
#if NIFT_CORO_MUTANT != 20
            if (value_call_[p]) work.insert(work.end(), values_.begin(), values_.end());
#endif
        }
    }

    // `thread(f, …)` (non-contractual switch): the compiler loads the named
    // function f (LoadFunction, possibly through Moves) into the callee
    // register just before the call (compiler_thread.cpp); any other shape is
    // treated as an unknown value call.
    static int thread_target(const std::vector<Instr>& code, std::size_t pc) {
        std::uint16_t reg = code[pc].b;
        for (std::size_t k = pc; k-- > 0;) {
            if (code[k].a != reg) continue;
            if (code[k].op == OpCode::LoadFunction) return code[k].b;
            if (code[k].op != OpCode::Move) return -1;
            reg = code[k].b;
        }
        return -1;
    }

    // A2: a cycle among task code (Call, Spawn, and value calls to every value).
    bool cyclic(std::string& reason, std::uint32_t& offset) const {
        const std::size_t n = program_.protos.size();
        std::vector<std::uint8_t> mark(n, 0);  // 0 new, 1 on the stack, 2 done
        for (std::size_t start = 0; start < n; ++start) {
            if (!task_[start] || mark[start]) continue;
            std::vector<std::pair<std::size_t, std::size_t>> stack{{start, 0}};
            mark[start] = 1;
            while (!stack.empty()) {
                const std::size_t p = stack.back().first;
                const std::vector<std::size_t> next = successors(p);
                if (stack.back().second == next.size()) {
                    mark[p] = 2;
                    stack.pop_back();
                    continue;
                }
                const std::size_t q = next[stack.back().second++];
                if (q >= n || !task_[q]) continue;
                if (mark[q] == 1) {
                    fail(q, 0, "coro-task-recursion", reason, offset);
                    return true;  // a cycle
                }
                if (mark[q] == 0) {
                    mark[q] = 1;
                    stack.push_back({q, 0});
                }
            }
        }
        return false;
    }
    std::vector<std::size_t> successors(std::size_t p) const {
        std::vector<std::size_t> out(edges_[p].begin(), edges_[p].end());
        if (value_call_[p]) out.insert(out.end(), values_.begin(), values_.end());
        return out;
    }

    // A3, A4 and the task part of A5.
    bool task_code(std::size_t p, std::string& reason, std::uint32_t& offset) const {
        const std::vector<Instr>& code = proto(p).code;
#if NIFT_CORO_MUTANT != 16
        std::size_t head = 0;
        if (!loops_suspend(proto(p), head)) return fail(p, head, "coro-task-loop", reason, offset);
#endif
        for (std::size_t pc = 0; pc < code.size(); ++pc) {
            const Instr& in = code[pc];
#if NIFT_CORO_MUTANT != 17
            if (!task_op_allowed(in.op))
                return fail(p, pc, std::string("coro-task-op:") + opcode_name(in.op), reason, offset);
#endif
            if (in.op != OpCode::CallNative) continue;
            if (in.b < kFirstBuiltin) {
                if (!observe_) return fail(p, pc, "coro-observe:print", reason, offset);
                continue;
            }
#if NIFT_CORO_MUTANT != 17
            if (!task_builtin_allowed(static_cast<BuiltinId>(in.b - kFirstBuiltin)))
                return fail(p, pc, "coro-task-native:" + native_name(in.b), reason, offset);
#endif
        }
        return true;
    }

    const Program& program_;
    bool observe_;
    std::vector<bool> task_;
    std::vector<bool> value_call_;
    std::vector<std::vector<std::uint16_t>> edges_;
    std::vector<std::uint16_t> values_;  // protos that are ever function values
};

}  // namespace

bool check_cooperative_admission(const Program& program, bool observe, std::string& reason,
                                 std::uint32_t& offset) {
    return Admission(program, observe).run(reason, offset);
}

void cooperative_switches(CompileLimits& limits) {
    const auto on = [](const char* name) {
        const char* v = std::getenv(name);
        return v != nullptr && v[0] == '1' && v[1] == '\0';
    };
    limits.cooperative_threads = on("NIFT_VM_COOPERATIVE_THREADS");
    limits.cooperative_observe = on("NIFT_VM_COOPERATIVE_OBSERVE");
    limits.parallel_tasks = !limits.cooperative_threads;
}

}  // namespace nift::rt
