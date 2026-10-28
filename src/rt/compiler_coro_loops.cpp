#include "rt/compiler_coro_loops.h"

#include "rt/native.h"
#include "rt/natives/builtin.h"

#include <vector>

// rt-lint: cold-file compile-time cooperative admission (A3), once per task proto

// Test-only deliberate defects (`-DNIFT_CORO_MUTANT=n`, tests/runtime2/cp22):
// 19 a loop is accepted when a genuine suspension lies anywhere in its body
// (the rejected round-5 rule: a `continue` path may skip it); 21 the identity
// condition is dropped: any call counts as a suspension (a call of a value,
// a unit function or another built-in, as a binding named `sleep` would
// compile); 22 the argument condition is dropped: any `Sleep` counts, whatever
// its duration (0, a variable). Normal builds contain none.
#ifndef NIFT_CORO_MUTANT
#define NIFT_CORO_MUTANT 0
#endif

namespace nift::rt {

namespace {

bool conditional_jump(OpCode op) {
    switch (op) {
        case OpCode::JmpFalse: case OpCode::JmpTrue: case OpCode::JmpNotNull: case OpCode::JmpNotLt:
        case OpCode::JmpNotLe: case OpCode::JmpNotGt: case OpCode::JmpNotGe: case OpCode::JmpNotEq:
        case OpCode::JmpNotNe: case OpCode::JmpIncLt: case OpCode::TryBegin:
            return true;
        default: return false;
    }
}

bool ends_path(OpCode op) {
    switch (op) {
        case OpCode::Return: case OpCode::ReturnNull: case OpCode::TaskEnd: case OpCode::Halt:
        case OpCode::TopReturn: case OpCode::Throw:
            return true;
        default: return false;
    }
}

// Control-flow successors. A TryBegin also leads to its handler: every
// instruction of the protected region may throw there, and every path into the
// region passes the TryBegin (structured code), so a cycle through a handler
// has a sub-cycle through the TryBegin that skips no suspension it would not.
std::vector<std::vector<std::size_t>> successors(const std::vector<Instr>& code) {
    std::vector<std::vector<std::size_t>> out(code.size());
    for (std::size_t pc = 0; pc < code.size(); ++pc) {
        const Instr& in = code[pc];
        const auto target = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(pc) + 1 + jump_offset(in.c));
        if (in.op == OpCode::Jmp) out[pc].push_back(target);
        else if (conditional_jump(in.op)) out[pc] = {pc + 1, target};
        else if (!ends_path(in.op)) out[pc].push_back(pc + 1);
    }
    return out;
}

class Loops {
public:
    explicit Loops(const Proto& proto) : proto_(proto), code_(proto.code), next_(successors(proto.code)) {
        target_.assign(code_.size() + 1, false);  // jump and handler targets (not fall-through)
        for (std::size_t pc = 0; pc < code_.size(); ++pc)
            for (std::size_t q : next_[pc])
                if (q != pc + 1 && q < target_.size()) target_[q] = true;
    }

    // Every cycle of the control-flow graph passes through a suspension; else
    // `head` is the instruction where an unsuspending cycle closes.
    bool every_cycle_suspends(std::size_t& head) const {
        const std::size_t n = code_.size();
        // Reachable from the entry in the full graph: code after a suspension
        // stays in scope even though the suspension's own edges are cut below.
        std::vector<bool> live(n, false);
        std::vector<std::size_t> work{0};
        while (!work.empty() && n > 0) {
            const std::size_t p = work.back();
            work.pop_back();
            if (p >= n || live[p]) continue;
            live[p] = true;
            for (std::size_t q : next_[p]) work.push_back(q);
        }
        std::vector<std::uint8_t> mark(n, 0);  // 0 new, 1 on the stack, 2 done
        for (std::size_t start = 0; start < n; ++start) {
            if (!live[start] || mark[start]) continue;
            std::vector<std::pair<std::size_t, std::size_t>> stack{{start, 0}};
            mark[start] = 1;
            while (!stack.empty()) {
                const std::size_t p = stack.back().first;
                const std::vector<std::size_t>& out = cut(p) ? none_ : next_[p];
                if (stack.back().second == out.size()) {
                    mark[p] = 2;
                    stack.pop_back();
                    continue;
                }
                const std::size_t q = out[stack.back().second++];
                if (q >= n) continue;
                if (mark[q] == 1 && !accepted(q, p)) {
                    head = q;
                    return false;
                }
                if (mark[q] == 0) {
                    mark[q] = 1;
                    stack.push_back({q, 0});
                }
            }
        }
        return true;
    }

private:
    // The genuine suspension primitive: `Sleep` (the built-in sleep, bound by
    // id at compile time) with one argument, which the same basic block loads
    // with an integer constant >= 1 (sleep(0) does not yield).
    bool suspension(std::size_t pc) const {
        const Instr& in = code_[pc];
        if (in.op != OpCode::Sleep || in.c != 1) return false;
#if NIFT_CORO_MUTANT == 22
        return true;  // M22: the argument condition is dropped
#endif
        for (std::size_t k = pc; k-- > 0;) {
            if (target_[k + 1]) return false;  // another path joins between the load and the Sleep
            if (code_[k].a != in.a) continue;
            if (code_[k].op == OpCode::LoadI) return jump_offset(code_[k].b) >= 1;
            if (code_[k].op != OpCode::LoadK || code_[k].b >= proto_.constants.size()) return false;
            const Value& v = proto_.constants[code_[k].b];
            return v.is(Kind::Int) && v.as_int() >= 1;
        }
        return false;
    }

    // Removing a suspension's outgoing edges leaves a graph that has a cycle
    // exactly when some cycle avoids every suspension.
    bool cut(std::size_t pc) const {
#if NIFT_CORO_MUTANT == 19
        (void)pc;
        return false;  // M19: suspensions are counted per loop body instead (accepted)
#else
#if NIFT_CORO_MUTANT == 21
        const OpCode op = code_[pc].op;
        if (op == OpCode::Call || op == OpCode::CallValue || op == OpCode::CallValueAny || op == OpCode::CallNative)
            return true;  // M21: any call counts as the suspension
#endif
        return suspension(pc);
#endif
    }

    bool accepted(std::size_t head, std::size_t tail) const {
#if NIFT_CORO_MUTANT == 19
        // M19: a suspension anywhere in the loop (head .. its last back edge).
        std::size_t end = tail;
        for (std::size_t r = head; r < code_.size(); ++r)
            for (std::size_t q : next_[r])
                if (q == head && r > end) end = r;
        for (std::size_t k = head; k <= end && k < code_.size(); ++k)
            if (suspension(k)) return true;
#endif
        (void)head;
        (void)tail;
        return false;
    }

    const Proto& proto_;
    const std::vector<Instr>& code_;
    std::vector<std::vector<std::size_t>> next_;
    std::vector<bool> target_;
    const std::vector<std::size_t> none_;
};

}  // namespace

bool loops_suspend(const Proto& proto, std::size_t& head) {
    return Loops(proto).every_cycle_suspends(head);
}

}  // namespace nift::rt
