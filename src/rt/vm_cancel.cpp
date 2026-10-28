#include "rt/vm_cancel.h"

#include "rt/error.h"
#include "rt/shared_state.h"

namespace nift::rt {

bool Vm::cancellation_requested() const { return task_cancelled(options_.current_task); }

bool Vm::raise_catchable(Fault& fault, std::string code, std::string message, std::uint32_t offset) {
    fault.reason = StopReason::Error;
    fault.message = std::move(message);
    fault.code = std::move(code);
    fault.catchable = true;
    SourceOrigin origin;
    if (!origin_at(offset, origin, fault)) {
        fault.catchable = false;
        fault.error = Value();
        return false;
    }
    Value error;
    std::string why;
    if (!make_system_error(fault.code, fault.message, origin, error, why)) {
        fault.catchable = false;
        fault.error = Value();
        return false;
    }
    fault.error = std::move(error);
    return false;
}

bool Vm::raise_cancelled(Fault& fault, const Proto* proto, std::uint32_t offset) {
    (void)proto;
    take_task_cancelled(options_.current_task);
    return raise_catchable(fault, "runtime.cancelled", "task: cancelled", offset);
}


}  // namespace nift::rt
