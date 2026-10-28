#include "rt/vm_report.h"

#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/error.h"
#include "rt/heap.h"
#include "rt/cell.h"
#include "rt/text_type.h"
#include "rt/vm_mutants.h"
#include "rt/native.h"
#include "rt/shared_state.h"
#include "rt/render.h"
#include "rt/vm_ops.h"

namespace nift::rt {

namespace {
inline bool truth(const Value& v) { return v.is(Kind::Bool) ? v.as_bool() : truthy(v); }
} // namespace
#define RK(x) (((x) & kConstantBit) ? K[(x) & 0x7fffu] : R[(x)])

// Evidence only: is `pc` running in code legacy runs on its prepared (loop)
// evaluator? Its own flag, or a call made from such code below it.
bool Vm::in_loop_path(const Instr* pc) const {
    if (pc->flags & kInstrLoopPath) return true;
    for (const Frame& frame : frames_)
        if ((frame.ret - 1)->flags & kInstrLoopPath) return true;
    return false;
}

void Vm::note_large(const Instr* pc, Fault& fault) {
    const bool operand = fault.large_operand;
    fault.large = fault.large_operand = false;
    ++large_numbers_;
    if (!in_loop_path(pc)) return;
    ++large_in_loop_path_;
    // Per-line attribution (review CP12 round 1, D4): the output line this
    // operation can affect is the next one printed (index lines_).
    const auto record = [this](std::vector<std::uint64_t>& lines) {
        if (!lines.empty() && lines.back() == lines_) return;
        if (lines.size() >= kMaxEvidenceLines) { evidence_truncated_ = true; return; }
        lines.push_back(lines_);
    };
    record(large_lines_);
    if (operand) record(derived_lines_);
}

std::uint32_t Vm::report_offset(const Fault& fault, const Proto* proto, const Instr* pc) const {
    std::uint32_t offset = proto->offsets[static_cast<std::size_t>(pc - proto->code.data())];
    if ((pc->op == OpCode::CellLoad || pc->op == OpCode::GetUpvalue) &&
        fault.message.rfind("reference target no longer exists: ", 0) == 0)
        offset = selector_offset(offset);
    if (fault.use_call_site) offset = main_offset(offset);
    MUTATE_ERROR_OFFSET(offset)
    return offset;
}

void Vm::error_result(VmResult& result, const Fault& fault, const Proto* proto, const Instr* pc) const {
    fault_into(result, fault);
    constexpr char kRecursion[] = "callable recursion depth exceeded";
    const bool recursion = result.message.rfind(kRecursion, 0) == 0;
    const bool uncaught_error = fault.reason == StopReason::Error && fault.catchable && fault.error.is(Kind::Error);
    if (uncaught_error) result.message = fault.error.heap_as<ErrorObject>()->message;
    if (fault.reason == StopReason::Error && !recursion) {
        bool in_expression_lambda = closure_ != nullptr && closure_->is_lambda && closure_->expression_body;
        for (const Frame& frame : frames_)
            in_expression_lambda =
                in_expression_lambda ||
                (frame.closure != nullptr && frame.closure->is_lambda && frame.closure->expression_body);
        if (in_expression_lambda) result.message = "lambda body error: " + result.message;
        // A vanished selector read in a lambda return belongs to that return
        // statement. Other expression errors retain the existing D-018 policy.
        bool return_call = (pc->flags & kInstrReturnCall) &&
                           (pc->op == OpCode::CellLoad || pc->op == OpCode::GetUpvalue) &&
                           fault.message.rfind("reference target no longer exists: ", 0) == 0;
        bool in_lambda = closure_ != nullptr && closure_->is_lambda;
        std::size_t error_returns = 0;
        for (const Frame& frame : frames_) {
            return_call = return_call || frame.return_call;
            in_lambda = in_lambda || (frame.closure != nullptr && frame.closure->is_lambda);
            if (frame.return_call) ++error_returns;
        }
        if (uncaught_error) {
            for (std::size_t k = 0; k < error_returns; ++k) result.message = "return: " + result.message;
        } else if (return_call && in_lambda && result.message.rfind("return: ", 0) != 0) {
            result.message = "return: " + result.message;
        }
    }
    if (uncaught_error) {
        const auto* error = fault.error.heap_as<ErrorObject>();
        result.error_origin = !error->source.empty() && error->line != 0 && error->column != 0;
        result.origin_source = result.error_origin ? error->source : std::string();
        result.origin_line = result.error_origin ? static_cast<std::uint32_t>(error->line) : 0;
        result.origin_column = result.error_origin ? static_cast<std::uint32_t>(error->column) : 0;
    }
    result.offset = report_offset(fault, proto, pc);
    collection_error_context(result, fault, proto, pc);
    result.main_offset = main_offset(result.offset);  // CP12b (vm_frames.cpp)
    result.error_in_loop_path = in_loop_path(pc);
}

bool Vm::native(const Instr& in, const Proto* proto, Value* R, std::string& output, Fault& fault) {
    for (std::uint16_t k = 0; k < in.c; ++k)
        if (is_cell(R[in.a + k])) return internal(fault, "cell reached a native");
    switch (static_cast<NativeId>(in.b)) {
        case NativeId::Print: {
            const std::size_t before = output.size();
            Value printable = R[in.a];
            if (printable.is(Kind::Handle) && printable.handle_kind() == HandleKind::Atomic) {
                SharedOpError error;
                Value loaded;
                if (!shared_atomic_load(printable, loaded, error))
                    return raise_catchable(fault, error.code.empty() ? "runtime.invalid_argument" : error.code,
                                           error.message, report_offset(fault, proto, &in));
                printable = std::move(loaded);
            }
            if (!native_print(printable, output, fault.message))
                return raise(fault, StopReason::Error, fault.message);
            for (std::size_t k = before; k < output.size(); ++k) lines_ += output[k] == '\n';
            R[in.a] = Value();
            flush_if_needed(output);
            return true;
        }
    }
    return builtin(in, proto, R, fault);  // CP14 built-ins; reports an unknown id
}

bool Vm::cold(const Instr& in, const Proto* proto, const Note* note, Value* R, const Value* K,
              std::string& output, Fault& fault) {
    Value out;
    switch (in.op) {
        case OpCode::Nop: return true;
        case OpCode::Div: if (!arith_slow(in, OpCode::Div, RK(in.b), RK(in.c), out, fault)) return false; break;
        case OpCode::Not: out = Value::boolean(!truth(R[in.b])); break;
        case OpCode::Truth: out = Value::boolean(truth(R[in.b])); break;
        case OpCode::Neg: if (!negate_slow(in, R[in.b], out, fault)) return false; break;
        case OpCode::Pos: if (!positive_slow(R[in.b], out, fault)) return false; break;
        case OpCode::CallNative: return native(in, proto, R, output, fault);
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
        case OpCode::GetIndex: if (!get_index(note, R[in.b], RK(in.c), out, fault)) return false; break;
        case OpCode::SetIndex: return set_index(note, R[in.a], RK(in.b), RK(in.c), fault);
        case OpCode::GetMember: if (!get_member(note, R[in.b], Atom{in.c}, out, fault)) return false; break;
        case OpCode::Push: return push_value(R[in.a], RK(in.b), fault, note);
        case OpCode::Length: if (!length_of(note, R[in.b], in.c != 0, out, fault)) return false; break;
        case OpCode::TypeMeet:
            out = Value::integer(
                meet_text_types(static_cast<int>(RK(in.b).as_int()), static_cast<int>(RK(in.c).as_int())));
            break;
        default: return struct_op(in, note, R, K, fault);  // CP12b structs (vm_struct.cpp)
    }
    R[in.a] = std::move(out);
    return true;
}

bool Vm::op_top_return(const Instr& in, Value* R, VmResult& result, Fault& fault) {
    return_value_ = R[in.a];
    has_return_value_ = true;
    result.returned = true;
    if (options_.return_value_only) return true;
    if (!append_rendered(R[in.a], result.return_text)) return internal(fault, "script return value");
    return true;
}

void Vm::result_stats(VmResult& result) {
    result.beyond_legacy_depth = beyond_legacy_depth_;
    result.large_numbers = large_numbers_;
    result.large_in_loop_path = large_in_loop_path_;
    result.large_lines = std::move(large_lines_);
    result.derived_lines = std::move(derived_lines_);
    result.evidence_truncated = evidence_truncated_;
    result.output_lines = lines_;
    result.heap_peak = heap_peak_;
    result.heap_live = live_heap_bytes();
}

} // namespace nift::rt
