#include "rt/host/host_error.h"

#include <iterator>

namespace nift::rt::host {

namespace {
// Design §2.6. `runtime.*` mappings of legacy's plain messages follow the
// CP13 table; where CP13 has no specific code yet the entry says so.
constexpr ErrorCodeInfo kCodes[] = {
    // catchable (legacy fail_recoverable sites)
    {"io.open_failed", HostStatus::Recoverable, true},
    {"io.read_failed", HostStatus::Recoverable, true},
    {"io.create_failed", HostStatus::Recoverable, true},
    {"io.write_failed", HostStatus::Recoverable, true},
    {"io.atomic_replace_failed", HostStatus::Recoverable, true},
    {"io.copy_failed", HostStatus::Recoverable, true},
    {"io.move_failed", HostStatus::Recoverable, true},
    {"io.remove_failed", HostStatus::Recoverable, true},
    {"io.directory_read_failed", HostStatus::Recoverable, true},
    {"io.change_directory_failed", HostStatus::Recoverable, true},
    {"io.metadata_failed", HostStatus::Recoverable, true},
    {"stream.open_failed", HostStatus::Recoverable, true},
    {"stream.read_failed", HostStatus::Recoverable, true},
    {"stream.write_failed", HostStatus::Recoverable, true},
    {"stream.flush_failed", HostStatus::Recoverable, true},
    {"stream.close_failed", HostStatus::Recoverable, true},
    {"json.parse_failed", HostStatus::Recoverable, true},
    // fatal argument / operation errors (not catchable today, D-022 O6)
    {"runtime.invalid_argument", HostStatus::Fatal, false},
    {"runtime.arity_error", HostStatus::Fatal, false},
    {"runtime.type_error", HostStatus::Fatal, false},
    {"runtime.invalid_operation", HostStatus::Fatal, false},
    // process model (§11; D-030)
    {"process.detach_in_build", HostStatus::Fatal, false},
    {"process.stdin_closed", HostStatus::Fatal, false},
    {"process.not_owned", HostStatus::Fatal, false},
    {"process.unsupported", HostStatus::Fatal, false},
    // limits (D-020 loud; D-028 read limit; never ordinary exceptions)
    {"limit.exceeded", HostStatus::Limit, false},
    // host services
    {"host.service_failed", HostStatus::Fatal, false},
    // invariant breaks (exit 70, never guessed)
    {"internal.host_call", HostStatus::Internal, false},
    {"internal.handle", HostStatus::Internal, false},
};
} // namespace

const char* status_name(HostStatus status) {
    switch (status) {
        case HostStatus::Ok: return "ok";
        case HostStatus::Recoverable: return "recoverable";
        case HostStatus::Fatal: return "fatal";
        case HostStatus::Limit: return "limit";
        case HostStatus::Internal: return "internal";
    }
    return "internal";
}

const ErrorCodeInfo* find_error_code(std::string_view code) {
    for (const auto& info : kCodes)
        if (code == info.code) return &info;
    return nullptr;
}

bool catchable(std::string_view code) {
    const auto* info = find_error_code(code);
    return info && info->catchable;
}

HostStatus status_for_code(std::string_view code) {
    const auto* info = find_error_code(code);
    return info ? info->status : HostStatus::Internal;
}

HostStatus make_error(HostError& error, std::string_view code, std::string message, int sys_error) {
    error.code = std::string(code);
    error.message = std::move(message);
    error.sys_error = sys_error;
    return status_for_code(code);
}

} // namespace nift::rt::host
