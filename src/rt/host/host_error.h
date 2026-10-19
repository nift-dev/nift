#pragma once
// CP16 Host layer: outcome classes, error codes and catchability (design §2.6).
//
// Identity (the code), severity (HostStatus) and catchability are separate
// (D-022). The code table below is the single place that says which codes are
// catchable; HostCall checks every reported failure against it, so a native
// cannot report a catchable code as fatal or the reverse.
#include <cstdint>
#include <string>
#include <string_view>

namespace nift::rt::host {

enum class HostStatus : std::uint8_t { Ok, Recoverable, Fatal, Limit, Internal };

const char* status_name(HostStatus status);

struct HostError {
    std::string code;     // "io.open_failed", "limit.exceeded", ...
    std::string message;  // exact user-visible text, built-in name prefix included
    int sys_error = 0;    // errno / GetLastError, for tests and JSON details only
};

struct ErrorCodeInfo {
    const char* code;
    HostStatus status;  // the only status this code may be reported with
    bool catchable;     // try/catch can observe it (today: the legacy fail_recoverable sites)
};

// Returns the table entry, or nullptr for a code the Host layer does not know.
const ErrorCodeInfo* find_error_code(std::string_view code);
// True only for known catchable codes.
bool catchable(std::string_view code);
// The status a code must be reported with; Internal for unknown codes.
HostStatus status_for_code(std::string_view code);

// Fills `error` and returns the status the code's table entry prescribes.
HostStatus make_error(HostError& error, std::string_view code, std::string message, int sys_error = 0);

} // namespace nift::rt::host
