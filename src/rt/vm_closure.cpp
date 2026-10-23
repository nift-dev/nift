#include "rt/vm_closure.h"

#include "rt/cell.h"
#include "rt/heap_account.h"

#include <unordered_map>

namespace nift::rt {

namespace {
const Note* note_at(const Proto* proto, const Instr* pc) {
    return proto->note(static_cast<std::size_t>(pc - proto->code.data()));
}
} // namespace

bool Vm::op_load_function(const Instr& in, Value* R, Fault& fault) {
    if (in.b >= protos_.size()) return internal(fault, "function proto");
    Value& value = named_functions_[in.b];
    if (value.is_null()) {
        auto* function = new FunctionObject();
        function->name = protos_[in.b]->name;
        function->code = protos_[in.b];
#if !(defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 8)
        heap_retally(function);
#endif
        value = Value::adopt(function);
    }
    R[in.a] = value;
    return memory_ok(fault);
}

bool Vm::op_new_cell(const Instr& in, Value* R, Fault& fault) {
    if (in.c >= stack_.size() || in.b >= stack_.size()) return internal(fault, "cell operands");
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 2
    // M2: share one cell per cell-creation site.  Keep the mutant state out of
    // Vm: a mutant-only class member changes the layout in only some of the
    // linked translation units (ODR/layout mismatch, which used to segfault).
    static std::unordered_map<const Instr*, Value> cells;
    auto entry = cells.try_emplace(&in);
    if (entry.second) entry.first->second = make_cell(R[in.b], R[in.c].as_int());
    R[in.a] = entry.first->second;
#else
    R[in.a] = make_cell(R[in.b], R[in.c].as_int());
#endif
    return memory_ok(fault);
}

bool Vm::op_cell_load(const Instr& in, Value* R, Fault& fault) {
    CellObject* cell = cell_of(R[in.b]);
    if (cell == nullptr) return internal(fault, "cell");
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 5
    R[in.a] = R[in.b];
#else
    R[in.a] = cell->value;
#endif
    return true;
}

bool Vm::op_cell_store(const Instr& in, Value* R, Fault& fault, const Proto* proto, std::int64_t text_type) {
    CellObject* cell = cell_of(R[in.a]);
    if (cell == nullptr) return internal(fault, "cell");
    const std::int64_t assigned = text_binding_type(text_type, R[in.b]);
    if (!tag_assignable(assigned, cell->binding_type)) {
        binding_type_error(assigned, cell->binding_type, note_at(proto, &in), fault);
        return false;
    }
#if !(defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 3)
    if (would_close_cycle(CycleTarget::captured_cell(cell), R[in.b], cycle_graph_)) {
        raise(fault, StopReason::Error, "reference cycle: the stored value would contain itself");
        fault.code = "runtime.reference_cycle";
        return false;
    }
#endif
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 1
    return true;
#else
    cell->value = R[in.b];
    if (may_contain_identity(cell->value)) mark_may_hold_identity(cell);
    return true;
#endif
}

bool Vm::op_closure(const Instr& in, Value* R, Fault& fault) {
    if (in.b >= protos_.size()) return internal(fault, "closure proto");
    auto* function = new FunctionObject();
    function->name = protos_[in.b]->name;
    function->code = protos_[in.b];
    function->is_lambda = true;
    function->expression_body = (in.flags & kInstrLambdaExprBody) != 0;
    function->upvalues.reserve(in.c);
    for (std::uint16_t k = 0; k < in.c; ++k) function->upvalues.push_back(R[in.a + 1 + k]);
#if !(defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 8)
    heap_retally(function);
#endif
    R[in.a] = Value::adopt(function);
    return memory_ok(fault);
}

bool Vm::op_get_upvalue(const Instr& in, Value* R, Fault& fault) {
    if (closure_ == nullptr || in.b >= closure_->upvalues.size()) return internal(fault, "upvalue");
    CellObject* cell = cell_of(closure_->upvalues[in.b]);
    if (cell == nullptr) return internal(fault, "upvalue cell");
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 5
    R[in.a] = closure_->upvalues[in.b];
#else
    R[in.a] = cell->value;
#endif
    return true;
}

bool Vm::op_get_upvalue_cell(const Instr& in, Value* R, Fault& fault) {
    if (closure_ == nullptr || in.b >= closure_->upvalues.size()) return internal(fault, "upvalue");
    R[in.a] = closure_->upvalues[in.b];
    return true;
}

bool Vm::op_set_upvalue(const Instr& in, Value* R, Fault& fault, const Proto* proto, std::int64_t text_type) {
    if (closure_ == nullptr || in.a >= closure_->upvalues.size()) return internal(fault, "upvalue");
    CellObject* cell = cell_of(closure_->upvalues[in.a]);
    if (cell == nullptr) return internal(fault, "upvalue cell");
    const std::int64_t assigned = text_binding_type(text_type, R[in.b]);
    if (!tag_assignable(assigned, cell->binding_type)) {
        binding_type_error(assigned, cell->binding_type, note_at(proto, &in), fault);
        return false;
    }
#if !(defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 3)
    if (would_close_cycle(CycleTarget::captured_cell(cell), R[in.b], cycle_graph_)) {
        raise(fault, StopReason::Error, "reference cycle: the stored value would contain itself");
        fault.code = "runtime.reference_cycle";
        return false;
    }
#endif
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 1
    return true;
#else
    cell->value = R[in.b];
    if (may_contain_identity(cell->value)) mark_may_hold_identity(cell);
    return true;
#endif
}

bool Vm::op_call(const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                 const Instr*& pc, Fault& fault) {
    const Proto* callee = protos_[in.b];
    const std::size_t callee_base = base + in.a;
    if ((frames_.size() >= next_frame_check_ || callee_base + callee->nregs + 1u > stack_.size()) &&
        !call_slow(callee, callee->name.c_str(), callee_base, fault))
        return false;
    frames_.push_back(Frame{pc + 1, base, proto, closure_, (in.flags & kInstrReturnCall) != 0});
    closure_ = nullptr;
    base = callee_base;
    proto = callee;
    R = stack_.data() + base;
    K = proto->constants.data();
    pc = proto->code.data();
    return true;
}

bool Vm::op_call_value(const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                       const Instr*& pc, Fault& fault) {
    const Value& callee = R[in.b];
    const FunctionObject* f = callee.is(Kind::Function) ? callee.heap_as<FunctionObject>() : nullptr;
    if (f == nullptr || f->code == nullptr) {
        const Note* note = note_at(proto, &in);
        if (in.flags & kInstrCallMember)
            raise(fault, StopReason::Error,
                  "unknown value or malformed expression: " + std::string(note ? note->text : ""));
        else
            raise(fault, StopReason::Error, "undefined callable: " + std::string(note ? note->text : ""));
        return false;
    }
    const Proto* code = static_cast<const Proto*>(f->code);
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 6
#define CALL_ARITY_OK true
#else
#define CALL_ARITY_OK (in.c == code->nparams)
#endif
    if (!(CALL_ARITY_OK)) {
        const Note* note = note_at(proto, &in);
        if (f->is_lambda)
            raise(fault, StopReason::Error, "lambda argument count mismatch");
        else if (in.flags & kInstrCallMember)
            raise(fault, StopReason::Error, "callable argument count mismatch: __nift_module_field");
        else
            raise(fault, StopReason::Error, "callable argument count mismatch: " + std::string(note ? note->text : ""));
        return false;
    }
#undef CALL_ARITY_OK
    const char* error_name = f->is_lambda
                                 ? nullptr
                                 : (in.flags & kInstrCallMember
                                        ? "__nift_module_field"
                                        : (note_at(proto, &in) ? note_at(proto, &in)->text.c_str() : ""));
    const std::size_t callee_base = base + in.a;
    if ((frames_.size() >= next_frame_check_ || callee_base + code->nregs + 1u > stack_.size()) &&
        !call_slow(code, error_name, callee_base, fault))
        return false;
    frames_.push_back(Frame{pc + 1, base, proto, closure_, (in.flags & kInstrReturnCall) != 0});
    closure_ = f;
    base = callee_base;
    proto = code;
    R = stack_.data() + base;
    K = proto->constants.data();
    pc = proto->code.data();
    return true;
}

bool Vm::op_undefined(const Instr& in, Fault& fault, const Proto* proto) {
    const Note* note = note_at(proto, &in);
    return raise(fault, StopReason::Error,
                 "unknown value or malformed expression: " + std::string(note ? note->text : ""));
}

} // namespace nift::rt
