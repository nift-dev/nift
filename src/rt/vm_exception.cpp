#include "rt/vm_exception.h"

#include "rt/error.h"
#include "rt/heap.h"
#include "rt/string.h"
#include "rt/vm.h"

#include <utility>

// rt-lint: cold-file exception unwinding, once per raised catchable Error

namespace nift::rt {

bool Vm::origin_at(std::uint32_t offset, SourceOrigin& out, Fault& fault) const {
    if (options_.source_map == nullptr) return internal(fault, "exception origin without source map");
    out = options_.source_map->at(offset);
    return true;
}

bool Vm::pop_handlers_for_frame(std::size_t depth, Fault& fault) {
    if (handlers_.empty() || handlers_.back().frame_depth < depth) return true;
#if defined(NIFT_EXC_MUTANT) && NIFT_EXC_MUTANT == 2 && !defined(NIFT_EXC_CHECKED)
    // E2 release build: pop defensively, as the unfixed VM would.
    while (!handlers_.empty() && handlers_.back().frame_depth >= depth) handlers_.pop_back();
    return true;
#else
    // TryEnd is emitted on every normal exit. A handler of the returning frame
    // still live here is stale; it is reported through the ordinary internal
    // path (statistics written), never by aborting.
    return raise(fault, StopReason::Internal, "stale exception handler");
#endif
}


bool Vm::handle_bail(Fault& fault, VmResult& result, Value*& R, const Value*& K, const Proto*& proto,
                     std::size_t& base, const Instr*& pc) {
    if (fault.large) note_large(pc, fault);
#if defined(NIFT_EXC_MUTANT) && (NIFT_EXC_MUTANT == 1 || NIFT_EXC_MUTANT == 5)
    if (fault.reason == StopReason::Error && !fault.catchable) {
        SourceOrigin origin;
        if (origin_at(report_offset(fault, proto, pc), origin, fault)) {
            fault.error = make_user_error(fault.message);
            fault.catchable = true;
        }
    }
#endif
    if (fault.reason == StopReason::Error && fault.catchable) {
        if (!fault.error.is(Kind::Error)) {
            fault.catchable = false;
            fault.error = Value();
            internal(fault, "catchable fault without Error payload");
        }
#if !defined(NIFT_EXC_MUTANT) || NIFT_EXC_MUTANT != 1
        else if (global_taken_ != 0) {
            fault.catchable = false;
            fault.error = Value();
            internal(fault, "global taken during an exception");
        }
#endif
        else if (unwind(fault, R, K, proto, base, pc)) {
            return true;
        }
    }
    error_result(result, fault, proto, pc);
    return false;
}

bool Vm::op_try_begin(const Instr& in, std::size_t base, const Proto* proto, const Instr* pc, Fault& fault) {
    Handler handler;
    handler.frame_depth = frames_.size();
    handler.base = base;
    handler.proto = proto;
    handler.catch_pc = static_cast<std::uint32_t>(pc - proto->code.data()) + 1u + jump_offset(in.c);
    handler.catch_reg = in.a;
    handler.reg_top = in.b;
    handlers_.push_back(handler);
    update_heap_limit();  // A15: handler storage is charged on reallocation
    return memory_ok(fault);
}

bool Vm::op_try_end(const Instr& in, Fault& fault) {
    (void)in;
#if defined(NIFT_EXC_MUTANT) && NIFT_EXC_MUTANT == 2
    if (in.c != 0) return true;  // E2: leave a handler on break/return exits
    (void)fault;
    if (handlers_.empty() || handlers_.back().frame_depth != frames_.size())
        return raise(fault, StopReason::Internal, "stale exception handler");
    handlers_.pop_back();
    return true;
#else
    if (handlers_.empty() || handlers_.back().frame_depth != frames_.size())
        return raise(fault, StopReason::Internal, "stale exception handler");
    handlers_.pop_back();
    return true;
#endif
}

bool Vm::op_throw(const Instr& in, Value* R, const Proto* proto, const Instr* pc, Fault& fault) {
    Value value = R[in.a];
    if (!value.is(Kind::Error)) {
        return raise(fault, StopReason::Error, "throw expression must evaluate to Error");
    }
    const auto* error = value.heap_as<ErrorObject>();
#ifdef NIFT_EXC_MUTANT
#if NIFT_EXC_MUTANT == 4
    const bool stamp = true;  // E4: restamp even an already-originated Error
#elif NIFT_EXC_MUTANT == 8
    if (error->source.empty() || error->line == 0 || error->column == 0) {
        SourceOrigin origin;
        if (!origin_at(proto->offsets[static_cast<std::size_t>(pc - proto->code.data())], origin, fault))
            return false;
        auto* shared = const_cast<ErrorObject*>(error);
        shared->source = origin.path;
        shared->line = origin.line;
        shared->column = origin.column;
        heap_retally(shared);
        value = R[in.a];
        error = value.heap_as<ErrorObject>();
        if (!memory_ok(fault)) return false;
        const bool stamp = false;
        (void)stamp;
    } else { const bool stamp = false; (void)stamp; }
#else
    const bool stamp = error->source.empty() || error->line == 0 || error->column == 0;
#endif
#else
    const bool stamp = error->source.empty() || error->line == 0 || error->column == 0;
#endif
#if !defined(NIFT_EXC_MUTANT) || NIFT_EXC_MUTANT != 8
    if (stamp) {
        SourceOrigin origin;
        if (!origin_at(proto->offsets[static_cast<std::size_t>(pc - proto->code.data())], origin, fault))
            return false;
        value = clone_error_with_origin(value, origin);
        if (!memory_ok(fault)) return false;
        error = value.heap_as<ErrorObject>();
    }
#endif
    fault.reason = StopReason::Error;
    fault.error = value;
    fault.code = error->code;
    fault.message = error->message;
    fault.catchable = true;
    return false;
}

bool Vm::op_error_new(const Instr& in, Value* R, Fault& fault) {
    if (in.c < 1 || in.c > 3)
        return raise(fault, StopReason::Error, "error: expected message, optional code, and optional cause");
    const Value& message = R[in.b];
    if (!message.is(Kind::String))
        return raise(fault, StopReason::Error, "error: message must be a string");
    std::string code = "user.raised";
    if (in.c >= 2) {
        if (!R[in.b + 1].is(Kind::String))
            return raise(fault, StopReason::Error, "error: code must be a string");
        code = std::string(string_view_of(R[in.b + 1]));
    }
    Value cause;
    if (in.c == 3) {
        if (!R[in.b + 2].is_null() && !R[in.b + 2].is(Kind::Error))
            return raise(fault, StopReason::Error, "error: cause must be Error or null");
        cause = R[in.b + 2];
    }
    Value out;
    std::string error;
    if (!make_error(std::string(string_view_of(message)), std::move(code), cause, {}, 0, 0, out, error))
        return raise(fault, StopReason::Error, "error: " + error);
    R[in.a] = std::move(out);
    return memory_ok(fault);
}

bool Vm::op_error_json(const Instr& in, Value* R, Fault& fault) {
    if (R[in.b].is_null()) {  // Error.cause is Error|Null; legacy stringifies null
        R[in.a] = make_string("null");
        return memory_ok(fault);
    }
    if (!R[in.b].is(Kind::Error)) return internal(fault, "ErrorJson receiver");
    R[in.a] = make_string(error_to_json(R[in.b], in.c != 0));
    return memory_ok(fault);
}

bool Vm::unwind(Fault& fault, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                const Instr*& pc) {
    if (fault.reason != StopReason::Error || !fault.catchable || !fault.error.is(Kind::Error)) return false;
    if (handlers_.empty()) return false;
    std::size_t selected = handlers_.size() - 1u;
#if defined(NIFT_EXC_MUTANT) && NIFT_EXC_MUTANT == 6
    selected = 0;  // E6: select outside-in
#endif
    const Handler handler = handlers_[selected];
    if (handler.frame_depth > frames_.size()) {
        raise(fault, StopReason::Internal, "stale exception handler");
        return false;
    }
    if (program_has_collection_boundaries_ &&
        !drain_collection_boundaries(fault, handler.frame_depth, proto, base, pc)) {
        raise(fault, StopReason::Internal, "collection boundary drain failed");
        return false;
    }
    while (frames_.size() > handler.frame_depth) {
        for (std::uint16_t k = 0; k < proto->nregs; ++k) R[k] = Value();
        const Frame frame = frames_.back();
        frames_.pop_back();
        base = frame.base;
        proto = frame.proto;
#if !defined(NIFT_EXC_MUTANT) || NIFT_EXC_MUTANT != 7
        closure_ = frame.closure;
#endif
        R = stack_.data() + base;
        K = proto->constants.data();
        pc = frame.ret;
    }
    base = handler.base;
    proto = handler.proto;
    R = stack_.data() + base;
    K = proto->constants.data();
#if defined(NIFT_EXC_MUTANT) && NIFT_EXC_MUTANT == 6
    handlers_.erase(handlers_.begin() + static_cast<std::ptrdiff_t>(selected), handlers_.end());
#else
    handlers_.pop_back();
#endif
    for (std::uint16_t k = handler.reg_top; k < proto->nregs; ++k) R[k] = Value();
    R[handler.catch_reg] = std::move(fault.error);
    fault = Fault{};
    pc = proto->code.data() + handler.catch_pc;
    return true;
}

} // namespace nift::rt
