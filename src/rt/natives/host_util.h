#pragma once
// Shared result/error plumbing for the CP16 host natives.
#include "rt/host/host_call.h"
#include "rt/natives/support.h"

#include <string>

namespace nift::rt::natives {

inline Status host_status(host::HostStatus status) {
    switch (status) {
        case host::HostStatus::Ok: return Status::Ok;
        case host::HostStatus::Recoverable: return Status::Recoverable;
        case host::HostStatus::Fatal: return Status::Error;
        case host::HostStatus::Limit: return Status::Limit;
        case host::HostStatus::Internal: return Status::Internal;
    }
    return Status::Internal;
}

inline host::HostError host_error(std::string code, std::string message) {
    host::HostError error;
    host::make_error(error, code, std::move(message));
    return error;
}

// Finishes a HostCall and maps its status. The HostCall may turn a misreported
// code into Internal; the mapped status follows the HostCall's final state.
inline Status fail_host(host::HostCall& call, host::HostStatus status, host::HostError error,
                        std::string& message) {
    const host::HostStatus final = call.fail(status, error);
    message = call.error().message;
    return host_status(final);
}

inline Status finish_host(host::HostCall& call, std::string res, const host::Observation& observation,
                          std::string& message) {
    const host::HostStatus final = call.succeed(std::move(res), observation);
    if (final == host::HostStatus::Ok) return Status::Ok;
    message = call.error().message;
    return host_status(final);
}

} // namespace nift::rt::natives
