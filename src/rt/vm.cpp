#include "rt/vm.h"

#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/native.h"
#include "rt/render.h"
#include "rt/text_type.h"

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

} // namespace

Vm::Vm(const Program& program, VmOptions options) : program_(program), options_(options) {
    for (const auto& proto : program_.protos) protos_.push_back(proto.get());
    stack_.resize(256);
    frames_.reserve(64);
}

void Vm::reserve_registers(std::size_t needed) {
    if (needed <= stack_.size()) return;
    std::size_t size = stack_.size() * 2;
    while (size < needed) size *= 2;
    stack_.resize(size);
}

bool Vm::flush_if_needed(std::string& output) {
    if (output.size() <= options_.output_limit || !options_.flush) return true;
    options_.flush(output, options_.flush_context);
    committed_ = true;
    return true;
}

#define RK(x) (((x) & kConstantBit) ? K[(x) & 0x7fffu] : R[(x)])
#if NIFT_RT_COMPUTED_GOTO
#define OP(name) L_##name:
#define DISPATCH() goto *kLabels[static_cast<std::size_t>(pc->op)]
#else
#define OP(name) case OpCode::name:
#define DISPATCH() continue
#endif
#define NEXT() { ++pc; DISPATCH(); }
#define FAIL() goto bail
#define ARITH(name, builtin, extra)                                                                  \
    OP(name) {                                                                                       \
        const Value& l = RK(pc->b); const Value& r = RK(pc->c); std::int64_t v;                      \
        if (both_int(l, r) && !builtin(l.as_int(), r.as_int(), &v) && safe_integer(v) extra) {       \
            R[pc->a] = Value::integer(v); NEXT();                                                    \
        }                                                                                            \
        if (!arith_slow(OpCode::name, l, r, R[pc->a], fault)) FAIL();                                \
        NEXT();                                                                                      \
    }
#define COMPARE(name, base, expr, left, right, store)                                                \
    OP(name) {                                                                                       \
        const Value& l = RK(left); const Value& r = RK(right); bool res;                             \
        if (both_int(l, r)) { const std::int64_t x = l.as_int(), y = r.as_int(); res = (expr); }     \
        else if (!compare_slow(OpCode::base, l, r, res, fault)) FAIL();                              \
        store;                                                                                       \
    }
#define SET_RESULT { R[pc->a] = Value::boolean(res); NEXT(); }
#define JUMP_UNLESS { pc += res ? 1 : 1 + jump_offset(pc->c); DISPATCH(); }
#define RETURN_TO_CALLER                                                                             \
    for (std::uint16_t k = 1; k < proto->nregs; ++k) if (R[k].is_heap()) R[k] = Value();             \
    { const Frame f = frames_.back(); frames_.pop_back(); base = f.base; proto = f.proto;            \
      R = stack_.data() + base; K = proto->constants.data(); pc = f.ret; }                           \
    DISPATCH();

VmResult Vm::run(std::string& output) {
    VmResult result;
    Fault fault;
    const Proto* proto = protos_.front();
    std::size_t base = 0;
    reserve_registers(proto->nregs + 1u);
    Value* R = stack_.data();
    const Value* K = proto->constants.data();
    const Instr* pc = proto->code.data();
#if NIFT_RT_COMPUTED_GOTO
#define NIFT_RT_LABEL(name) &&L_##name,
    static const void* const kLabels[] = {NIFT_RT_OPCODES(NIFT_RT_LABEL)};
#undef NIFT_RT_LABEL
    DISPATCH();
#else
    for (;;) switch (pc->op) {
#endif
    OP(LoadK) { R[pc->a] = K[pc->b]; NEXT(); }
    OP(LoadI) { R[pc->a] = Value::integer(jump_offset(pc->b)); NEXT(); }
    OP(LoadNull) { R[pc->a] = Value(); NEXT(); }
    OP(LoadBool) { R[pc->a] = Value::boolean(pc->b != 0); NEXT(); }
    OP(Move) { R[pc->a] = R[pc->b]; NEXT(); }
    ARITH(Add, __builtin_add_overflow, )
    ARITH(Sub, __builtin_sub_overflow, )
    ARITH(Mul, __builtin_mul_overflow, && (v != 0 || (l.as_int() >= 0 && r.as_int() >= 0)))
    OP(Mod) {
        const Value& l = RK(pc->b); const Value& r = RK(pc->c);
        if (both_int(l, r) && r.as_int() != 0) {
            const std::int64_t v = l.as_int() % r.as_int();
            if (v != 0 || l.as_int() >= 0) { R[pc->a] = Value::integer(v); NEXT(); }
        }
        if (!arith_slow(OpCode::Mod, l, r, R[pc->a], fault)) FAIL();
        NEXT();
    }
    OP(AddI) {
        Value& x = R[pc->b];
        if (x.is(Kind::Int) && safe_integer(x.as_int() + jump_offset(pc->c))) {
            R[pc->a] = Value::integer(x.as_int() + jump_offset(pc->c)); NEXT();
        }
        if (!x.is_number()) {
            raise(fault, BailReason::Error, "increment/decrement requires a numeric lvalue");
            FAIL();
        }
        if (!arith_slow(OpCode::Add, x, Value::integer(jump_offset(pc->c)), R[pc->a], fault)) FAIL();
        NEXT();
    }
    OP(AddAssign) {
        Value& x = R[pc->a]; const Value& y = RK(pc->b); std::int64_t v;
        if (both_int(x, y) && !__builtin_add_overflow(x.as_int(), y.as_int(), &v) && safe_integer(v)) {
            x = Value::integer(v); NEXT();
        }
        if (!add_assign_slow(x, y, x, fault)) FAIL();
        NEXT();
    }
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
    OP(BindType) { R[pc->a] = Value::integer(text_binding_type(RK(pc->c).as_int(), R[pc->b])); NEXT(); }
    OP(CheckAssign) {
        if (!check_assign(R[pc->a], R[pc->b].as_int(), RK(pc->c).as_int(), fault)) FAIL();
        NEXT();
    }
    OP(CheckType) {
        const std::int64_t to = R[pc->b].as_int();
        if ((R[pc->a].is(Kind::Int) && to == 2) || tag_assignable(binding_tag(R[pc->a]), to)) NEXT();
        raise(fault, BailReason::Error, "cannot assign a value of another type to a typed binding");
        FAIL();
    }
    OP(Call) {
        const Proto* callee = protos_[pc->b];
        if (frames_.size() >= options_.max_frames) {
            raise(fault, BailReason::RecursionBudget, "callable recursion depth exceeded");
            FAIL();
        }
        const std::size_t callee_base = base + pc->a;
        reserve_registers(callee_base + callee->nregs + 1u);
        frames_.push_back(Frame{pc + 1, base, proto});
        base = callee_base;
        proto = callee;
        R = stack_.data() + base;
        K = proto->constants.data();
        pc = proto->code.data();
        DISPATCH();
    }
    OP(Return) {
        if (pc->c != 0 && is_aggregate(R[pc->a])) { raise(fault, BailReason::Aliasing, "aggregate return"); FAIL(); }
        if (pc->a != 0) R[0] = R[pc->a];
        RETURN_TO_CALLER
    }
    OP(ReturnNull) { R[0] = Value(); RETURN_TO_CALLER }
    OP(TopReturn) {
        if (!append_rendered(R[pc->a], result.return_text)) {
            raise(fault, BailReason::Error, "script return value is not directly renderable");
            FAIL();
        }
        result.returned = true;
        goto done;
    }
    OP(Halt) { goto done; }
    OP(Bail) { raise(fault, static_cast<BailReason>(pc->b), "the legacy engine reports an error here"); FAIL(); }
    OP(Nop) OP(Div) OP(Not) OP(Truth) OP(Neg) OP(Pos) OP(CallNative) OP(NewArray) OP(NewObject)
    OP(ObjectSet) OP(CheckParam) OP(TypeMeet) OP(GetIndex) OP(SetIndex) OP(GetMember) OP(Push) OP(Length) OP(NoAlias) {
        if (!cold(*pc, R, K, output, fault)) FAIL();
        NEXT();
    }
#if !NIFT_RT_COMPUTED_GOTO
    default: raise(fault, BailReason::Unverified, "bad opcode"); FAIL();
    }
#endif
bail:
    result.reason = fault.reason;
    result.message = fault.message;
    result.offset = proto->offsets[static_cast<std::size_t>(pc - proto->code.data())];
done:
    result.committed = committed_;
    return result;
}

bool Vm::native(const Instr& in, Value* R, std::string& output, Fault& fault) {
    switch (static_cast<NativeId>(in.b)) {
        case NativeId::Print:
            if (!native_print(R[in.a], output, fault.message)) {
                fault.reason = BailReason::Error;
                return false;
            }
            R[in.a] = Value();
            return flush_if_needed(output);
    }
    return raise(fault, BailReason::Unverified, "unknown native");
}

bool Vm::cold(const Instr& in, Value* R, const Value* K, std::string& output, Fault& fault) {
    Value out;
    switch (in.op) {
        case OpCode::Nop: return true;
        case OpCode::Div: if (!arith_slow(OpCode::Div, RK(in.b), RK(in.c), out, fault)) return false; break;
        case OpCode::Not: out = Value::boolean(!truth(R[in.b])); break;
        case OpCode::Truth: out = Value::boolean(truth(R[in.b])); break;
        case OpCode::Neg: if (!negate_slow(R[in.b], out, fault)) return false; break;
        case OpCode::Pos: if (!positive_slow(R[in.b], out, fault)) return false; break;
        case OpCode::CallNative: return native(in, R, output, fault);
        case OpCode::NewArray:
            out = make_array(in.c);
            for (std::uint16_t k = 0; k < in.c; ++k) array_push(out, R[in.b + k]);
            break;
        case OpCode::NewObject: out = make_object(); break;
        case OpCode::ObjectSet: {
            Value value = RK(in.c);
            object_set(R[in.a], Atom{in.b}, std::move(value));
            return true;
        }
        case OpCode::GetIndex: if (!get_index(R[in.b], RK(in.c), out, fault)) return false; break;
        case OpCode::SetIndex: return set_index(R[in.a], RK(in.b), RK(in.c), fault);
        case OpCode::GetMember: if (!get_member(R[in.b], Atom{in.c}, out, fault)) return false; break;
        case OpCode::Push: return push_value(R[in.a], RK(in.b), fault);
        case OpCode::Length: if (!length_of(R[in.b], out, fault)) return false; break;
        case OpCode::CheckParam:
            if (text_binding_type(RK(in.c).as_int(), R[in.a]) != binding_tag(R[in.a]))
                return raise(fault, BailReason::Unverified, "parameter type differs between legacy paths");
            return true;
        case OpCode::TypeMeet:
            out = Value::integer(
                meet_text_types(static_cast<int>(RK(in.b).as_int()), static_cast<int>(RK(in.c).as_int())));
            break;
        case OpCode::NoAlias:
            if (is_aggregate(R[in.a])) return raise(fault, BailReason::Aliasing, "aggregate bound from a place");
            return true;
        default: return raise(fault, BailReason::Unverified, "unexpected opcode");
    }
    R[in.a] = std::move(out);
    return true;
}

} // namespace nift::rt
