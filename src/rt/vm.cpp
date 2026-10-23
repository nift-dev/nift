#include "rt/vm.h"

#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/compare.h"
#include "rt/heap_account.h"
#include "rt/native.h"
#include "rt/render.h"
#include "rt/text_type.h"
#include "rt/vm_mutants.h"

#include <algorithm>
#include <iterator>
#include <utility>

// Dispatch: computed goto on GCC/Clang (labels-as-values), a portable switch
// otherwise or when NIFT_RT_VM_SWITCH is defined.
#if defined(__GNUC__) && !defined(NIFT_RT_VM_SWITCH)
#define NIFT_RT_COMPUTED_GOTO 1
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wgnu-label-as-value"
#else
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#else
#define NIFT_RT_COMPUTED_GOTO 0
#endif

namespace nift::rt {

namespace {

inline bool both_int(const Value& a, const Value& b) { return a.is(Kind::Int) && b.is(Kind::Int); }
inline bool truth(const Value& v) { return v.is(Kind::Bool) ? v.as_bool() : truthy(v); }
VmResult register_budget_exceeded() {
    VmResult result;
    result.reason = StopReason::Internal;
    result.message = "register stack budget exceeded";
    return result;
}

} // namespace

Vm::Vm(const Program& program, VmOptions options) : program_(program), options_(options) {
    for (const auto& proto : program_.protos) protos_.push_back(proto.get());
    named_functions_.resize(protos_.size());
    stack_.resize(256);
    global_ready_.resize(256);
    frames_.reserve(64);
    if (next_frame_check_ > options_.max_frames) next_frame_check_ = options_.max_frames;
    update_heap_limit();
}

void Vm::update_heap_limit() {
    const std::size_t stacks = stack_.capacity() * sizeof(Value) + frames_.capacity() * sizeof(Frame);
    heap_limit_ = stacks < options_.heap_budget ? options_.heap_budget - stacks : 0;
}

bool Vm::memory_exceeded(Fault& fault) {
    update_heap_limit();
    if (live_heap_bytes() <= heap_limit_) return true;
    if (frames_.size() >= kLegacyMaxDepth) {
        beyond_legacy_depth_ = true;
        return raise(fault, StopReason::Error, "callable recursion depth exceeded (memory budget)");
    }
    return raise(fault, StopReason::Internal, "memory budget exceeded");
}

bool Vm::reserve_registers(std::size_t needed) {
    if (needed <= stack_.size()) return true;
    if (needed > options_.max_registers) return false;
    std::size_t size = stack_.size() * 2;
    while (size < needed) size *= 2;
    stack_.resize(size < options_.max_registers ? size : options_.max_registers);
    update_heap_limit();
    return true;
}

// Slow path of Call: frame checks at depth next_frame_check_, then register
// stack growth (or its budget).
bool Vm::call_slow(const Proto* callee, const char* error_name, std::size_t callee_base, Fault& fault) {
    if (frames_.size() >= next_frame_check_ && !frame_check(callee, error_name, fault)) return false;
    if (!reserve_registers(callee_base + callee->nregs + 1u)) {
        if (frames_.size() >= kLegacyMaxDepth) {
            beyond_legacy_depth_ = true;
            return raise(fault, StopReason::Error,
                         error_name ? std::string("callable recursion depth exceeded: ") + error_name
                                    : std::string("callable recursion depth exceeded"));
        }
        return raise(fault, StopReason::Internal, "register stack budget exceeded");
    }
    return memory_ok(fault);
}

// Taken at depth next_frame_check_: the frame budget and the legacy depth
// mark (memory is checked by memory_ok).
bool Vm::frame_check(const Proto* callee, const char* error_name, Fault& fault) {
    static_cast<void>(callee);
    const std::size_t depth = frames_.size();
    if (depth >= options_.max_frames) {
        beyond_legacy_depth_ = true;
        return raise(fault, StopReason::Error,
                     error_name ? std::string("callable recursion depth exceeded: ") + error_name
                                : std::string("callable recursion depth exceeded"));
    }
    if (depth >= kLegacyMaxDepth) beyond_legacy_depth_ = true;
    next_frame_check_ = depth < kLegacyMaxDepth ? kLegacyMaxDepth : depth + depth / 8 + 1;
    if (next_frame_check_ > options_.max_frames) next_frame_check_ = options_.max_frames;
    return true;
}

void Vm::flush_if_needed(std::string& output) {
    if (output.size() > options_.flush_threshold && options_.flush) options_.flush(output, options_.flush_context);
}

namespace {
inline const Note* note_at(const Proto* proto, const Instr* pc) {
    return proto->note(static_cast<std::size_t>(pc - proto->code.data()));
}
} // namespace
#define RK(x) (((x) & kConstantBit) ? K[(x) & 0x7fffu] : R[(x)])
#if NIFT_RT_COMPUTED_GOTO
#define OP(name) L_##name:
#define DISPATCH() goto *labels[static_cast<std::size_t>(pc->op)]
#define CHECKED(name) LC_##name:
#define UNCHECKED true
#else
#define OP(name) case OpCode::name:
#define DISPATCH() continue
#define CHECKED(name)
#define UNCHECKED !checked
#endif
#define NEXT() { ++pc; DISPATCH(); }
#define FAIL() goto bail
// A number legacy calls large now exists (only slow paths create one): from
// here on the integer arithmetic opcodes take their slow paths ("checked"
// dispatch), so every large operand is seen and recorded as evidence
// (review CP12 round 1, D4) at no cost to programs without large numbers.
#define NOTE_LARGE if (fault.large) { note_large(pc, fault); CHECK_FROM_NOW_ON }
#if NIFT_RT_COMPUTED_GOTO
#define CHECK_FROM_NOW_ON labels = checked_labels;
#else
#define CHECK_FROM_NOW_ON checked = true;
#endif
#define ARITH(name, builtin, extra)                                                                  \
    OP(name) {                                                                                       \
        const Value& l = RK(pc->b); const Value& r = RK(pc->c); std::int64_t v;                      \
        if (UNCHECKED && both_int(l, r) && !builtin(l.as_int(), r.as_int(), &v) && safe_integer(v)   \
            extra) {                                                                                 \
            R[pc->a] = Value::integer(v); NEXT();                                                    \
        }                                                                                            \
    }                                                                                                \
    CHECKED(name)                                                                                    \
    if (!arith_slow(*pc, OpCode::name, RK(pc->b), RK(pc->c), R[pc->a], fault) || !memory_ok(fault))  \
        FAIL();                                                                                      \
    NOTE_LARGE NEXT();
#define COMPARE(name, base, expr, left, right, store)                                                \
    OP(name) {                                                                                       \
        const Value& l = RK(left); const Value& r = RK(right); bool res;                             \
        if (both_int(l, r)) { const std::int64_t x = l.as_int(), y = r.as_int(); res = (expr); }     \
        else if (!compare_slow(OpCode::base, l, r, res, fault)) FAIL();                              \
        store;                                                                                       \
    }
#define SET_RESULT { R[pc->a] = Value::boolean(res); NEXT(); }
#define JUMP_UNLESS { pc += res ? 1 : 1 + jump_offset(pc->c); DISPATCH(); }
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 4
#define RESTORE_CLOSURE(f) ((void)(f))  /* M4: leave the callee closure installed */
#else
#define RESTORE_CLOSURE(f) (closure_ = (f).closure)
#endif
#define RETURN_TO_CALLER                                                                             \
    for (std::uint16_t k = 1; k < proto->nregs; ++k) if (R[k].is_heap()) R[k] = Value();             \
    { const Frame f = frames_.back(); frames_.pop_back(); base = f.base; proto = f.proto;            \
      RESTORE_CLOSURE(f);                                                                            \
      R = stack_.data() + base; K = proto->constants.data(); pc = f.ret; }                          \
    DISPATCH();

VmResult Vm::run(std::string& output) {
    VmResult result;
    Fault fault;
    const Proto* proto = protos_.front();
    std::size_t base = 0;
    if (!reserve_registers(proto->nregs + 1u)) return register_budget_exceeded();
    Value* R = stack_.data();
    const Value* K = proto->constants.data();
    const Instr* pc = proto->code.data();
#if NIFT_RT_COMPUTED_GOTO
#define NIFT_RT_LABEL(name) &&L_##name,
    static const void* const kLabels[] = {NIFT_RT_OPCODES(NIFT_RT_LABEL)};
#undef NIFT_RT_LABEL
    const void* checked_labels[std::size(kLabels)];
    std::copy(std::begin(kLabels), std::end(kLabels), checked_labels);
    for (const auto& [op, label] : {std::pair{OpCode::Add, &&LC_Add}, {OpCode::Sub, &&LC_Sub},
                                    {OpCode::Mul, &&LC_Mul}, {OpCode::Mod, &&LC_Mod}, {OpCode::AddI, &&LC_AddI},
                                    {OpCode::AddAssign, &&LC_AddAssign}})
        checked_labels[static_cast<std::size_t>(op)] = label;
    MUTATE_CHECKED_TABLE(checked_labels)
    const void* const* labels = kLabels;
    DISPATCH();
#else
    bool checked = false;
    for (;;) switch (pc->op) {
#endif
    OP(LoadK) { R[pc->a] = K[pc->b]; NEXT(); } OP(LoadI) { R[pc->a] = Value::integer(jump_offset(pc->b)); NEXT(); }
    OP(LoadNull) { R[pc->a] = Value(); NEXT(); } OP(LoadBool) { R[pc->a] = Value::boolean(pc->b != 0); NEXT(); }
    OP(Move) { R[pc->a] = R[pc->b]; NEXT(); }
    ARITH(Add, __builtin_add_overflow, )
    ARITH(Sub, __builtin_sub_overflow, )
    ARITH(Mul, __builtin_mul_overflow, && (v != 0 || (l.as_int() >= 0 && r.as_int() >= 0)))
    OP(Mod) {
        const Value& l = RK(pc->b); const Value& r = RK(pc->c);
        if (UNCHECKED && both_int(l, r) && r.as_int() != 0 && r.as_int() != -1) {
            const std::int64_t v = MUTATE_MOD(l.as_int() % r.as_int());
            if (v != 0 || l.as_int() >= 0) { R[pc->a] = Value::integer(v); NEXT(); }
        }
    }
    CHECKED(Mod)
    if (!arith_slow(*pc, OpCode::Mod, RK(pc->b), RK(pc->c), R[pc->a], fault)) FAIL();
    NOTE_LARGE NEXT();
    OP(AddI) {
        const Value& x = R[pc->b];
        if (UNCHECKED && x.is(Kind::Int) && safe_integer(x.as_int()) && safe_integer(x.as_int() + jump_offset(pc->c))) {
            MUTATE_ADDI(x, jump_offset(pc->c))
            R[pc->a] = Value::integer(x.as_int() + jump_offset(pc->c)); NEXT();
        }
    }
    CHECKED(AddI)
    if (!arith_slow(*pc, OpCode::Add, R[pc->b], Value::integer(jump_offset(pc->c)), R[pc->a], fault)) FAIL();
    NOTE_LARGE NEXT();
    OP(AddAssign) {
        Value& x = R[pc->a]; const Value& y = RK(pc->b); std::int64_t v;
        if (UNCHECKED && both_int(x, y) && !__builtin_add_overflow(x.as_int(), y.as_int(), &v) && safe_integer(v)) {
            x = Value::integer(v); NEXT();
        }
    }
    CHECKED(AddAssign)
    if (!add_assign_slow(*pc, R[pc->a], RK(pc->b), fault) || !memory_ok(fault)) FAIL();
    NOTE_LARGE NEXT();
    COMPARE(Eq, Eq, x == y, pc->b, pc->c, SET_RESULT)
    COMPARE(Ne, Ne, x != y, pc->b, pc->c, SET_RESULT)
    COMPARE(Lt, Lt, x < y, pc->b, pc->c, SET_RESULT)
    COMPARE(Le, Le, x <= y, pc->b, pc->c, SET_RESULT)
    COMPARE(Gt, Gt, x > y, pc->b, pc->c, SET_RESULT)
    COMPARE(Ge, Ge, x >= y, pc->b, pc->c, SET_RESULT)
    COMPARE(JmpNotLt, Lt, x < y, pc->a, pc->b, JUMP_UNLESS)
    COMPARE(JmpNotLe, Le, x <= y, pc->a, pc->b, JUMP_UNLESS)
    COMPARE(JmpNotGt, Gt, x > y, pc->a, pc->b, JUMP_UNLESS)
    COMPARE(JmpNotGe, Ge, x >= y, pc->a, pc->b, JUMP_UNLESS)
    COMPARE(JmpNotEq, Eq, x == y, pc->a, pc->b, JUMP_UNLESS)
    COMPARE(JmpNotNe, Ne, x != y, pc->a, pc->b, JUMP_UNLESS)
    OP(Jmp) { pc += 1 + jump_offset(pc->c); DISPATCH(); }
    OP(JmpFalse) { pc += truth(R[pc->a]) ? 1 : 1 + jump_offset(pc->c); DISPATCH(); }
    OP(JmpTrue) { pc += truth(R[pc->a]) ? 1 + jump_offset(pc->c) : 1; DISPATCH(); }
    OP(JmpNotNull) { pc += R[pc->a].is_null() ? 1 : 1 + jump_offset(pc->c); DISPATCH(); }
    OP(BindType) { R[pc->a] = Value::integer(text_binding_type(BIND_TEXT(pc->c), R[pc->b])); NEXT(); }
    OP(CheckAssign) {  // textual `x = e`: the type legacy derives from the text and the value
        const std::int64_t assigned = text_binding_type(RK(pc->c).as_int(), R[pc->a]);
        if (tag_assignable(assigned, R[pc->b].as_int())) NEXT();
        binding_type_error(assigned, R[pc->b].as_int(), note_at(proto, pc), fault); FAIL();
    }
    OP(CheckType) {
        const std::int64_t to = R[pc->b].as_int(); const std::int64_t tag = fast_tag(R[pc->a]);
        if (tag_assignable(tag, to)) NEXT();
        binding_type_error(tag, to, note_at(proto, pc), fault); FAIL();
    }
    OP(Call) {
        if (!op_call(*pc, R, K, proto, base, pc, fault)) FAIL();
        DISPATCH();
    }
    OP(CallValue) { if (!op_call_value(*pc, R, K, proto, base, pc, fault)) FAIL(); DISPATCH(); }
    OP(LoadFunction) { if (!op_load_function(*pc, R, fault)) FAIL(); NEXT(); }
    OP(NewCell) { if (!op_new_cell(*pc, R, fault)) FAIL(); NEXT(); }
    OP(CellLoad) { if (!op_cell_load(*pc, R, fault)) FAIL(); NEXT(); }
    OP(CellStore) { if (!op_cell_store(*pc, R, fault, proto, RK(pc->c).as_int())) FAIL(); NEXT(); }
    OP(Closure) { if (!op_closure(*pc, R, fault)) FAIL(); NEXT(); }
    OP(GetUpvalue) { if (!op_get_upvalue(*pc, R, fault)) FAIL(); NEXT(); }
    OP(GetUpvalueCell) { if (!op_get_upvalue_cell(*pc, R, fault)) FAIL(); NEXT(); }
    OP(SetUpvalue) { if (!op_set_upvalue(*pc, R, fault, proto, RK(pc->c).as_int())) FAIL(); NEXT(); }
    OP(Undefined) { if (!op_undefined(*pc, fault, proto)) FAIL(); NEXT(); }
    OP(IterCheck) OP(ArrayElem) OP(ArrayElemValue) OP(ObjectKeys) OP(ObjectLookup)
    OP(ObjectLookupValue) OP(IterMissing) {
        if (!op_iter(*pc, R, proto, fault)) FAIL();
        NEXT(); }
    OP(Return) {
        if (pc->a != 0) R[0] = R[pc->a];
        RETURN_TO_CALLER
    }
    OP(ReturnNull) { R[0] = Value(); RETURN_TO_CALLER }
    OP(TopReturn) {
        if (!append_rendered(R[pc->a], result.return_text)) {
            internal(fault, "script return value");
            FAIL();
        }
        result.returned = true;
        goto done;
    }
    OP(Halt) { goto done; }
    OP(Nop) OP(Div) OP(Not) OP(Truth) OP(Neg) OP(Pos) OP(CallNative) OP(NewArray) OP(NewObject)
    OP(ObjectSet) OP(TypeMeet) OP(GetIndex) OP(SetIndex) OP(GetMember) OP(Push) OP(Length) OP(SlotOf) OP(NewStruct)
    OP(StructAdd) OP(FieldGet) OP(FieldSet) OP(CheckField) {
        if (!cold(*pc, note_at(proto, pc), R, K, output, fault) || !memory_ok(fault))
            FAIL();
        NOTE_LARGE NEXT();
    }
    OP(GetGlobal) { R[pc->a] = stack_[pc->b]; NEXT(); }
    OP(GetGlobalChecked) { if (!op_get_global_checked(*pc, R, fault, proto)) FAIL(); NEXT(); }
    OP(SetGlobal) { stack_[pc->a] = R[pc->b]; NEXT(); } OP(MarkGlobal) { op_mark_global(*pc); NEXT(); }
    OP(CheckGlobal) { if (!op_check_global(*pc, fault, proto)) FAIL(); NEXT(); }
#if !NIFT_RT_COMPUTED_GOTO
    default: raise(fault, StopReason::Internal, "bad opcode"); FAIL();
    }
#endif
bail:
    if (fault.large) note_large(pc, fault);
    error_result(result, fault, proto, pc);
done:
    result.beyond_legacy_depth = beyond_legacy_depth_;
    result.large_numbers = large_numbers_;
    result.large_in_loop_path = large_in_loop_path_;
    result.large_lines = std::move(large_lines_);
    result.derived_lines = std::move(derived_lines_);
    result.evidence_truncated = evidence_truncated_;
    result.output_lines = lines_;
    result.heap_peak = heap_peak_;
    result.heap_live = live_heap_bytes();
    return result;
}

bool Vm::op_get_global_checked(const Instr& in, Value* R, Fault& fault, const Proto* proto) {
    if (in.b >= global_ready_.size() || !global_ready_[in.b]) {
        const Note* note = note_at(proto, &in);
        raise(fault, StopReason::Error,
              "unknown value or malformed expression: " + std::string(note ? note->text : ""));
        if (in.flags & kInstrIndexKey) fault.use_call_site = true;
        return false;
    }
    R[in.a] = stack_[in.b];
    return true;
}

void Vm::op_mark_global(const Instr& in) {
    if (in.a >= global_ready_.size()) global_ready_.resize(in.a + 1u);
    global_ready_[in.a] = 1;
}

bool Vm::op_check_global(const Instr& in, Fault& fault, const Proto* proto) {
    if (in.a < global_ready_.size() && global_ready_[in.a]) return true;
    const Note* note = note_at(proto, &in);
    raise(fault, StopReason::Error,
          "assignment to undefined binding: " + std::string(note ? note->text : ""));
    return false;
}

} // namespace nift::rt
