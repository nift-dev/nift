#include "rt/vm_builtin.h"

#include "FileSystem.h"
#include "HostTrace.h"

#include "rt/native.h"
#include "rt/error.h"
#include "rt/natives/builtin.h"
#include "rt/shared_state.h"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <thread>
#include <utility>

// The VM's call into the CP14 built-in natives (CallNative with an id from
// kFirstBuiltin on): off the hot path, like every CallNative (Vm::cold).

namespace nift::rt {

namespace {
namespace fs = std::filesystem;

// B1: the opt-in NIFT_FS_ROOT policy, built once from the same canonical
// invocation setting legacy's Parser reads. No root means unrestricted.
const host::PathPolicy& vm_path_policy() {
    static const host::PathPolicy policy = [] {
        host::PathPolicy result;
        const char* configured = std::getenv("NIFT_FS_ROOT");
        if (!configured || !*configured) return result;
        std::error_code error;
        fs::path root = fs::absolute(configured, error).lexically_normal();
        if (!error) {
            const fs::path canonical = fs::weakly_canonical(root, error);
            if (!error) root = canonical.lexically_normal();
        }
        const fs::path root_copy = root;
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 5
        result.allow = [root_copy](const fs::path& candidate, std::string& reason) {
            if (filesystem::path_within(root_copy, candidate)) return true;
            reason = "path escapes configured filesystem root: " + candidate.generic_string();
            return false;
        };
#else
        (void)root_copy;
#endif
        return result;
    }();
    return policy;
}
} // namespace

// CP14: a pure built-in over R[a .. a+c) (receiver first for a method). The
// natives store `out` last, so the result may replace R[a] directly; the
// argument registers are temporaries of this call and are cleared.
bool Vm::builtin(const Instr& in, const Proto* proto, Value* R, Fault& fault) {
    const std::size_t id = static_cast<std::size_t>(in.b - kFirstBuiltin);
    if (id >= natives::builtin_count()) return raise(fault, StopReason::Internal, "unknown built-in");
    const natives::BuiltinSpec& spec = natives::builtin_spec(static_cast<natives::BuiltinId>(id));
    natives::Context context;
    context.heap_limit = heap_limit_;
    context.host.trace = nift::trace::enabled() ? &nift::rt::host::TraceChannel::process_default() : nullptr;
    context.host.paths = &vm_path_policy();
    context.host.error_code = &context.host_error_code;  // N1: per-call Host code
    context.handles =
        handles::HandleAccess{&handle_table_, handle_context_open_ ? handle_context_ : handles::kDetached};
    context.protos = transfer_map_;
    context.task = options_.current_task;
    context.owner_token =
        options_.current_task
            ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(options_.current_task.get()))
            : static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())) | 1u;
    Value out;
    std::string message;
    switch (natives::call_builtin(spec, context, &R[in.a], in.c, out, message)) {
        case natives::Status::Ok: break;
        case natives::Status::Error:
            if (!context.host_error_code.empty()) fault.code = context.host_error_code;
            return raise(fault, StopReason::Error, std::move(message));
        case natives::Status::Recoverable: {
            // CP15: legacy raises a catchable Error. N1: the HostCall's own
            // code (io.open_failed / io.read_failed / ...) wins over the
            // per-built-in fallback; validate has no HostCall and keeps
            // schema.rejected.
#if defined(NIFT_HOST_MUTANT) && NIFT_HOST_MUTANT == 14
            // N1 negative control: restore the old per-built-in mapping.
            fault.code = std::string(natives::recoverable_code(spec.id));
#else
            fault.code = context.host_error_code.empty()
                             ? std::string(natives::recoverable_code(spec.id))
                             : context.host_error_code;
#endif
            fault.catchable = true;
            raise(fault, StopReason::Error, std::move(message));
            SourceOrigin origin;
            if (!origin_at(report_offset(fault, proto, &in), origin, fault)) {
                fault.catchable = false;
                fault.error = Value();
                return false;
            }
            Value error;
            std::string why;
            if (!make_system_error(fault.code, fault.message, origin, error, why)) {
                fault.catchable = false;
                fault.error = Value();
                return raise(fault, StopReason::Internal, "catchable Error construction: " + why);
            }
            fault.error = std::move(error);
            return false;
        }
        case natives::Status::Limit:
            // CP15 review R2: a fatal, uncatchable resource limit.
            fault.code = "limit.exceeded";
            fault.catchable = false;
            return raise(fault, StopReason::Error, std::move(message));
        case natives::Status::Internal:
            // A receiver or argument kind the compiler promised away.
            return raise(fault, StopReason::Internal,
                         "unexpected operands (compiler defect): built-in " + std::string(spec.name));
        case natives::Status::Memory:
            // The native stopped building its result over the budget (the
            // partial result is already freed): always a stop, never a retry.
            update_heap_limit();
            if (frames_.size() >= kLegacyMaxDepth) {
                beyond_legacy_depth_ = true;
                return raise(fault, StopReason::Error, "callable recursion depth exceeded (memory budget)");
            }
            return raise(fault, StopReason::Internal, "memory budget exceeded");
    }
    R[in.a] = std::move(out);
    for (std::uint16_t k = 1; k < in.c; ++k) R[in.a + k] = Value();
    return true;
}

} // namespace nift::rt
