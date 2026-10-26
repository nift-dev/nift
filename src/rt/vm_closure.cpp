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
#if !(defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 5)
    if (protos_[in.b]->session_source) function->program_owner = protos_[in.b]->session_owner.lock();
#endif
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
    if (in.op == OpCode::NewSourceSlot) R[in.a] = make_cell(R[in.a], kSourceSlot);
    return memory_ok(fault);
}

bool Vm::op_cell_load(const Instr& in, Value* R, Fault& fault, const Note* note) {
    CellObject* cell = cell_of(R[in.b]);
    if (cell == nullptr) return internal(fault, "cell");
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 5
    R[in.a] = R[in.b];
#else
    std::string missing;
    if (!cell_read(R[in.b], R[in.a], missing)) {
        if (missing.empty()) return internal(fault, "cell location");
        return raise(fault, StopReason::Error, "reference target no longer exists: " + (note ? note->text : missing));
    }
#endif
    return true;
}

bool Vm::op_cell_cached(const Instr& in, Value* R, Fault& fault) {
    std::string missing;
    bool ok = false;
    if (in.op == OpCode::CellTake) { R[in.a] = cell_take(R[in.b]); return true; }
    if (in.op == OpCode::CellAnchor) { R[in.a] = cell_anchor(R[in.b]); return true; }
    if (in.op == OpCode::CellSlot) { R[in.a] = cell_slot(R[in.b]); return true; }
    if (in.op == OpCode::CellLoadCached) ok = cell_cached_read(R[in.b], R[in.a], missing);
    else {
#if !(defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 3)
        Value owner = cell_cached_owner(R[in.a]);
        if (would_close_cycle(CycleTarget::captured_cell(cell_of(owner)), R[in.b], cycle_graph_)) {
            raise(fault, StopReason::Error, "reference cycle: the stored value would contain itself");
            fault.code = "runtime.reference_cycle";
            return false;
        }
#endif
        ok = cell_cached_put(R[in.a], R[in.b], missing);
    }
    if (ok) return memory_ok(fault);
    if (missing.empty()) return internal(fault, "cached cell");
    return raise(fault, StopReason::Error, "reference target no longer exists: " + missing);
}

bool Vm::op_cell_store(const Instr& in, Value* R, Fault& fault, const Proto* proto, std::int64_t text_type) {
    CellObject* cell = cell_of(R[in.a]);
    if (cell == nullptr) return internal(fault, "cell");
    const std::int64_t assigned = text_binding_type(text_type, R[in.b]);
    if (!tag_assignable(assigned, cell_binding_type(*cell))) {
        binding_type_error(assigned, cell_binding_type(*cell), note_at(proto, &in), fault);
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
    cell_rebind(R[in.a], R[in.b]);
    return memory_ok(fault);
#endif
}

bool Vm::op_closure(const Instr& in, Value* R, Fault& fault) {
    if (in.b >= protos_.size()) return internal(fault, "closure proto");
    auto* function = new FunctionObject();
    function->name = protos_[in.b]->name;
    function->code = protos_[in.b];
#if !(defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 5)
    if (protos_[in.b]->session_source) function->program_owner = protos_[in.b]->session_owner.lock();
#endif
    function->is_lambda = true;
    function->display_id = next_callable_display_id_++;
    function->expression_body = (in.flags & kInstrLambdaExprBody) != 0;
    function->selector_call_site = function->expression_body &&
        ((in.flags & kInstrLambdaReturnOrigin) || (closure_ && closure_->selector_call_site));
    function->upvalues.reserve(in.c);
    for (std::uint16_t k = 0; k < in.c; ++k) function->upvalues.push_back(R[in.a + 1 + k]);
#if !(defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 8)
    heap_retally(function);
#endif
    R[in.a] = Value::adopt(function);
    return memory_ok(fault);
}

bool Vm::op_get_upvalue(const Instr& in, Value* R, Fault& fault, const Note* note) {
    if (closure_ == nullptr || in.b >= closure_->upvalues.size()) return internal(fault, "upvalue");
    CellObject* cell = cell_of(closure_->upvalues[in.b]);
    if (cell == nullptr) return internal(fault, "upvalue cell");
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 5
    R[in.a] = closure_->upvalues[in.b];
#else
    std::string missing;
    if (!cell_read(closure_->upvalues[in.b], R[in.a], missing)) {
        if (missing.empty()) return internal(fault, "upvalue location");
        return raise(fault, StopReason::Error, "reference target no longer exists: " + (note ? note->text : missing));
    }
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
    if (!tag_assignable(assigned, cell_binding_type(*cell))) {
        binding_type_error(assigned, cell_binding_type(*cell), note_at(proto, &in), fault);
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
    Value slot = closure_->upvalues[in.a];
    cell_rebind(slot, R[in.b]);
    return memory_ok(fault);
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
    const Proto* code = callable_code(f, proto);
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
