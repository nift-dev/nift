#pragma once
// The Runtime 2.0 register VM (CP11): executes a compiled Program.
//
// Frames live on a heap frame stack (std::vector<Frame>) and registers on a
// heap register stack; a Nift-to-Nift call pushes a frame and continues in
// the same dispatch loop, so calls never recurse on the C++ stack
// (architecture.md #4). A callee's registers start at the caller's argument
// window (arguments are passed without copying). Recursion depth is a frame
// budget (semantics.md "Recursion", approved change D-001).
//
// CP12: a run either completes or stops with a program error that the VM
// reports itself (legacy's message format, the position of the failing
// expression, exit status 1; D-018c), or with an internal failure (exit 70). Nothing is ever re-run
// on the legacy engine: the engine was selected before the first
// instruction. Output is streamed: the buffer is handed to `flush` whenever
// it grows past `flush_threshold` and at the end of the run.
//
// Recursion (approved change D-001#4): the frame budget replaces legacy's
// 64-frame limit; beyond it the run stops with the error "callable recursion
// depth exceeded: <function>" (legacy fails the same program at depth 64
// with that error family).
//
// Memory (review R1, round 2 R5): the register stack is bounded
// (`max_registers`) and the whole run is bounded by `heap_budget`: the exact
// live rt heap of the isolate (rt/heap_account.h: every string, array,
// object, ... however deeply nested, counted when created or resized) plus
// the register and frame stacks. It is checked after every instruction that
// can allocate (all on slow paths) and when the stacks grow. Exceeding it
// deeper than legacy's 64 frames is the recursion error above; at a depth
// legacy also reaches it is an Internal failure (exit 70). Either way the
// process stops before the host runs out of memory.
#include "rt/handles/table.h"
#include "rt/heap_account.h"
#include "Diagnostic.h"
#include "rt/proto.h"
#include "rt/source_map.h"
#include "rt/value_cycle.h"
#include "rt/vm_ops.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nift::rt {

inline constexpr std::size_t kMaxEvidenceLines = 4096;

// Legacy limit of nested user-callable frames (ParserHelpers.cpp kMaxCallableDepth).
inline constexpr std::size_t kLegacyMaxDepth = 64;

struct VmOptions {
    std::size_t max_frames = 100000;
    std::size_t max_registers = std::size_t(16) << 20;      // 16M values, 256 MiB
    std::size_t heap_budget = std::size_t(1) << 30;  // live rt heap + register and frame stacks (bytes)
    std::size_t flush_threshold = std::size_t(64) << 10;
    // Called with the buffered output when it exceeds flush_threshold (and
    // by the caller at the end). Null: everything stays in the buffer.
    void (*flush)(std::string& output, void* context) = nullptr;
    void* flush_context = nullptr;
    // CP13B: origins for Error values. Null is allowed for runs that cannot
    // raise an Error; an origin requested without a map is an internal stop.
    const SourceMap* source_map = nullptr;
    // Optional owned result capture (CP19 expression entry). Cold TopReturn
    // path only; caller keeps the Value alive through run and serialization.
    // Callable values still require their Program lifetime, as elsewhere.
    Value* return_value = nullptr;
    std::vector<Value>* session_globals = nullptr;  // stable cells/types, owned by the session
};

struct VmResult {
    // Cold uncaught-report context; never applied to a caught Error.
    std::vector<nift::detail::DiagnosticFrame> diagnostic_frames;
    StopReason reason = StopReason::None;  // None: the program completed
    std::string message;                   // legacy's message (Error) or the failure (Internal)
    std::uint32_t offset = 0;              // source offset of the failing expression
    // The source offset of main's instruction active at the stop (its call
    // when the stop is in a callee): where in the top-level code the program
    // was (CP12b modules: which module was initialising).
    std::uint32_t main_offset = 0;
    bool returned = false;                 // the script ended with `return value`
    std::string return_text;               // rendering of that value
    bool beyond_legacy_depth = false;      // the call depth exceeded legacy's 64 frames
    // Evidence for the D-018 difference policies (never a change of
    // behaviour): arithmetic that met a number legacy calls large (or went
    // out of range), how much of it ran in code legacy runs on its prepared
    // (loop) evaluator, and whether the error did.
    std::uint64_t large_numbers = 0;
    std::uint64_t large_in_loop_path = 0;
    bool error_in_loop_path = false;
    // Per-line evidence (review CP12 round 1, D4): for each such operation in
    // loop-evaluator code, the index of the output line printed next (the
    // number of lines printed before it), deduplicated; `derived_lines` only
    // those with a large operand. Capped at kMaxEvidenceLines entries each
    // (evidence_truncated: later lines are unattributed).
    std::vector<std::uint64_t> large_lines;
    std::vector<std::uint64_t> derived_lines;
    bool evidence_truncated = false;
    std::uint64_t output_lines = 0;  // newlines printed by the program
    std::size_t heap_peak = 0;       // peak live rt heap seen by budget checks (test evidence)
    std::size_t heap_live = 0;       // live rt heap when the run stopped
    std::string code;        // the stop's diagnostic code when it has a specific one (Fault::code)
    bool catchable = false;  // legacy raises it as a catchable Error
    // CP13B Q5: origin of an uncaught Error, copied out for lifetime safety.
    bool error_origin = false;
    std::string origin_source;
    std::uint32_t origin_line = 0, origin_column = 0;
    bool return_inspection = false;
    SourceOrigin report_origin;  // cold session diagnostics; separate from the Error payload
    std::string statement_text;  // buffered REPL presentation, discarded on failure
};

// The stop's reason, message, code and catchability.
inline void fault_into(VmResult& result, const Fault& fault) {
    result.reason = fault.reason;
    result.message = fault.message;
    result.code = fault.code;
    result.catchable = fault.catchable;
}

class Vm {
public:
    Vm(const Program& program, VmOptions options = VmOptions());
    ~Vm();
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;
    VmResult finish_session_resources(bool cancelled = false);
    VmResult run(std::string& output);
    VmResult run_session_unit(const Program& unit, const std::vector<const Proto*>& linked, std::string& output);
    void clear_session_execution();
    void retire_session_slots(const std::vector<std::uint16_t>& slots);

private:
    Value& global_slot(std::size_t slot) {
        return options_.session_globals ? options_.session_globals->at(slot) : stack_[slot];
    }
    struct Frame {
        const Instr* ret = nullptr;
        std::size_t base = 0;
        const Proto* proto = nullptr;
        const FunctionObject* closure = nullptr;
        bool return_call = false;
    };
    struct Handler {
        std::size_t frame_depth = 0;
        std::size_t base = 0;
        const Proto* proto = nullptr;
        std::uint32_t catch_pc = 0;
        std::uint16_t catch_reg = 0;
        std::uint16_t reg_top = 0;
    };
    bool reserve_registers(std::size_t needed);
    void open_handle_runtime();
    void complete_run(VmResult& result);
    void close_handle_runtime(VmResult& result, bool on_error = false);
    static void left_open_summary(const handles::CloseReport& close, VmResult& result);
    // CP17 unwind release: a caught Error is unwinding `register_value`'s
    // register (natives::abandon_file_on_unwind has the exact rule).
    void abandon_unwound_handle(const Value& register_value);
    // The source offset of main's active call (vm_frames.cpp); `own` when main
    // itself stopped.
    std::uint32_t main_offset(std::uint32_t own) const;
    std::uint32_t selector_offset(std::uint32_t own) const;
    bool frame_check(const Proto* callee, const char* error_name, Fault& fault);
#if defined(__GNUC__)
    __attribute__((noinline, cold))
#endif
    bool call_slow(const Proto* callee, const char* error_name, std::size_t callee_base, Fault& fault);
    bool cold(const Instr& in, const Proto* proto, const Note* note, Value* R, const Value* K,
              std::string& output, Fault& fault);
    // CP12C closure opcodes (vm_closure.cpp).
    bool op_load_function(const Instr& in, Value* R, Fault& fault);
    bool op_session_render(const Instr&, Value*, VmResult&, Fault&, const Proto*);
    bool op_top_return(const Instr& in, Value* R, VmResult& result, Fault& fault, const Proto* proto);
    void result_stats(VmResult& result);
    bool op_new_cell(const Instr& in, Value* R, Fault& fault);
    bool op_cell_cached(const Instr& in, Value* R, Fault& fault);
    bool op_cell_load(const Instr& in, Value* R, Fault& fault, const Note* note);
    bool op_cell_store(const Instr& in, Value* R, Fault& fault, const Proto* proto, std::int64_t text_type);
    bool op_closure(const Instr& in, Value* R, Fault& fault);
    bool op_get_upvalue(const Instr& in, Value* R, Fault& fault, const Note* note);
    bool op_get_upvalue_cell(const Instr& in, Value* R, Fault& fault);
    bool op_set_upvalue(const Instr& in, Value* R, Fault& fault, const Proto* proto, std::int64_t text_type);
    bool op_call(const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                 const Instr*& pc, Fault& fault);
    bool op_call_value(const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                       const Instr*& pc, Fault& fault);
    bool op_undefined(const Instr& in, Fault& fault, const Proto* proto);
    bool op_get_global_checked(const Instr& in, Value* R, Fault& fault, const Proto* proto);
    void op_mark_global(const Instr& in);
    bool op_check_global(const Instr& in, Fault& fault, const Proto* proto);
    bool op_meta_global(const Instr& in, Value* R, Fault& fault, const Proto* proto);
    // CP13B: exception dispatch (vm_exception.cpp).
    bool unwind(Fault& fault, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base, const Instr*& pc);
    bool pop_handlers_for_frame(std::size_t depth, Fault& fault);
    bool origin_at(const Proto* proto, std::uint32_t offset, SourceOrigin& out, Fault& fault) const;
    bool op_array_mutate(const Instr&, Value*, Fault&, const Proto*);
    bool collection(const Instr&,Value*,Fault&,const Proto*);
    bool op_collection_sort(const Instr&, Value*, Fault&, const Proto*);
    bool op_collection_step(const Instr&, Value*, Fault&);
    bool op_collection_call(const Instr&, Value*&, const Value*&, const Proto*&, std::size_t&, const Instr*&, Fault&);
    bool op_try_begin(const Instr& in, std::size_t base, const Proto* proto, const Instr* pc, Fault& fault);
    bool op_try_end(const Instr& in, Fault& fault);
    bool op_throw(const Instr& in, Value* R, const Proto* proto, const Instr* pc, Fault& fault);
    bool op_error_new(const Instr& in, Value* R, Fault& fault);
    bool op_error_json(const Instr& in, Value* R, Fault& fault);
    bool handle_bail(Fault& fault, VmResult& result, Value*& R, const Value*& K, const Proto*& proto,
                     std::size_t& base, const Instr*& pc);
    // CP12C slice 2 default `for` opcodes (vm_iter.cpp).
    bool op_iter_check(const Instr& in, Value* R, Fault& fault);
    bool op_array_elem(const Instr& in, Value* R, Fault& fault);
    bool op_array_elem_value(const Instr& in, Value* R, Fault& fault);
    bool op_object_keys(const Instr& in, Value* R, Fault& fault);
    bool op_object_lookup(const Instr& in, Value* R, Fault& fault);
    bool op_object_lookup_value(const Instr& in, Value* R, Fault& fault);
    bool op_iter_missing(const Instr& in, Fault& fault, const Proto* proto);
    bool op_iter(const Instr& in, Value* R, const Proto* proto, Fault& fault);
    bool native(const Instr& in, const Proto* proto, Value* R, std::string& output, Fault& fault);
    bool builtin(const Instr& in, const Proto* proto, Value* R, Fault& fault);  // CP14 built-ins
    Value* struct_location(std::int64_t);
    bool struct_op(const Instr& in, const Note* note, Value* R, const Value* K, Fault& fault);  // vm_struct.cpp
    void flush_if_needed(std::string& output);
    bool in_loop_path(const Instr* pc) const;
    void note_large(const Instr* pc, Fault& fault);
    void collection_error_context(VmResult&, const Fault&, const Proto*, const Instr*) const;
    void error_result(VmResult& result, const Fault& fault, const Proto* proto, const Instr* pc) const;
    std::uint32_t report_offset(const Fault& fault, const Proto* proto, const Instr* pc) const;
    bool memory_ok(Fault& fault) {
        const std::size_t live = live_heap_bytes();
        if (live > heap_peak_) heap_peak_ = live;
        return live <= heap_limit_ || memory_exceeded(fault);
    }
#if defined(__GNUC__)
    __attribute__((noinline, cold))
#endif
    bool memory_exceeded(Fault& fault);
    void update_heap_limit();

    bool drain_collection_boundaries(const Fault&, std::size_t retained_depth,
                                      const Proto*, std::size_t base, const Instr* pc);
    bool program_has_collection_boundaries_ = false;
    const Program& program_;
    VmOptions options_;
    std::shared_ptr<handles::HandleTable> handle_table_owner_;
    handles::HandleTable& handle_table_;  // one Script context per run or persistent session
    handles::ContextId handle_context_ = 0;
    bool handle_context_open_ = false;
    std::vector<Value> stack_;
    std::vector<std::uint8_t> global_ready_;
    std::vector<Frame> frames_;
    std::vector<Handler> handlers_;
    std::vector<const Proto*> protos_;
    std::vector<Value> named_functions_;  // canonical named-function values (LoadFunction)
    const FunctionObject* closure_ = nullptr;  // the running closure
    CycleGraph cycle_graph_;
    std::uint64_t large_numbers_ = 0;
    std::uint64_t large_in_loop_path_ = 0;
    std::uint64_t lines_ = 0;  // newlines printed so far (evidence attribution)
    std::size_t heap_peak_ = 0;
    std::vector<std::uint64_t> large_lines_, derived_lines_;
    bool evidence_truncated_ = false;
    bool beyond_legacy_depth_ = false;
    std::size_t next_frame_check_ = kLegacyMaxDepth;  // Call takes its slow path at this depth
    std::size_t heap_limit_ = 0;  // heap_budget minus the stacks' bytes: the live-heap bound
    std::size_t global_taken_ = 0;  // C6: globals nulled by global_take and not yet put back
    std::uint64_t next_callable_display_id_ = 1;
    std::uint64_t next_session_callable_id_ = 1;
};

} // namespace nift::rt
