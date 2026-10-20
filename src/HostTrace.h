#pragma once
// Runtime 2.0 CP08: Host-operation trace for differential testing.
//
// When the environment variable NIFT_TRACE_HOST names an open file descriptor
// (for example `NIFT_TRACE_HOST=3 nift build 3>trace.jsonl`), every traced Host
// operation writes one JSON line to it:
//
//   {"ctx":"<page or script>","op":"<operation>","arg":"<path/name>","res":"<result class>"}
//
// The format is specified in docs/runtime-2/trace-format.md. When the variable
// is unset, `enabled()` is a single load of a cached flag and nothing else runs.
// Tracing never changes any other output (enforced by tests/runtime2/
// trace_neutrality.sh).
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <io.h>
#include <stdlib.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace nift::trace {

namespace detail {
// The descriptor is validated and taken over once, as early as possible
// (nift::trace::initialize() at the start of main; lazily otherwise):
//  - it must be open and writable at that moment, so a descriptor the caller
//    closed can never be confused with a file Nift opens later (review: a
//    closed fd 3 was reused by page outputs, which then received trace lines);
//  - 0, 1 and 2 are refused unless NIFT_TRACE_HOST_ALLOW_STDIO=1, so trace
//    lines never mix into stdout/stderr by accident;
//  - it is duplicated to a private close-on-exec descriptor and the original
//    is closed, so child processes never inherit it;
//  - NIFT_TRACE_HOST (and the allow switch) are removed from the environment,
//    so scripts (getenv/env()) and children see the same environment with and
//    without tracing.
inline int take_descriptor() {
    const char* value = std::getenv("NIFT_TRACE_HOST");
    if (!value) return -1;
    // An empty value disables tracing but is still removed from the
    // environment (round-2 review: NIFT_TRACE_HOST="" stayed visible to env()).
    char* end = nullptr;
    const long parsed = *value ? std::strtol(value, &end, 10) : -1;
    const char* allow = std::getenv("NIFT_TRACE_HOST_ALLOW_STDIO");
    const bool allow_stdio = allow && std::strcmp(allow, "1") == 0;
    int fd = (!*value || !end || *end != '\0' || parsed < 0 || parsed > (1L << 20)) ? -1 : static_cast<int>(parsed);
    if (fd >= 0 && fd <= 2 && !allow_stdio) fd = -1;
#ifdef _WIN32
    if (fd >= 0 && ::_get_osfhandle(fd) == -1) fd = -1;
    _putenv_s("NIFT_TRACE_HOST", "");
    _putenv_s("NIFT_TRACE_HOST_ALLOW_STDIO", "");
    return fd;
#else
    if (fd >= 0) {
        struct stat info;
        const int flags = ::fcntl(fd, F_GETFL);
        if (::fstat(fd, &info) != 0 || flags < 0 || (flags & O_ACCMODE) == O_RDONLY) fd = -1;
    }
    if (fd >= 0) {
        const int owned = ::fcntl(fd, F_DUPFD_CLOEXEC, 100);
        if (owned < 0) fd = -1;
        else { if (fd > 2) ::close(fd); fd = owned; }
    }
    ::unsetenv("NIFT_TRACE_HOST");
    ::unsetenv("NIFT_TRACE_HOST_ALLOW_STDIO");
    return fd;
#endif
}
// CP16: NIFT_TRACE_HOST_VERSION ("2" selects the v2 channel; unset, empty,
// "1" or anything else keep v1) is taken together with the descriptor and
// removed from the environment like NIFT_TRACE_HOST, so scripts and children
// see the same environment with and without tracing.
inline int& version_slot() {
    static int value = 1;
    return value;
}
inline int take_version() {
    const char* value = std::getenv("NIFT_TRACE_HOST_VERSION");
    const int version = value && std::strcmp(value, "2") == 0 ? 2 : 1;
    if (value) {
#ifdef _WIN32
        _putenv_s("NIFT_TRACE_HOST_VERSION", "");
#else
        ::unsetenv("NIFT_TRACE_HOST_VERSION");
#endif
    }
    return version;
}
inline int descriptor() {
    static const int fd = [] {
        const bool requested = std::getenv("NIFT_TRACE_HOST") != nullptr;
        const int taken = take_descriptor();
        if (requested) version_slot() = take_version();
        return taken;
    }();
    return fd;
}
// 1 or 2 (meaningful while tracing is enabled).
inline int version() {
    (void)descriptor();
    return version_slot();
}
inline std::mutex& write_mutex() {
    static std::mutex mutex;
    return mutex;
}
// Per-thread context: the tracked page being built, or "script" for a
// standalone script. Builds may run pages on several threads, so the harness
// compares traces per context, in order within each context.
inline std::string& context() {
    thread_local std::string value = "script";
    return value;
}
// Async tasks and threads are attributed to the context that created them,
// numbered in creation order within that context ("<ctx>/async#1"): creation
// happens in program order on the owning thread, so labels are stable across
// runs and engines, and each task's operations are compared in order.
inline std::string child_label(const char* kind) {
    thread_local std::unordered_map<std::string, unsigned> counters;
    const std::string owner = context();
    const unsigned n = ++counters[owner + "|" + kind];
    return owner + "/" + kind + "#" + std::to_string(n);
}
inline void escape(std::string& out, const std::string& text) {
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
}
} // namespace detail

inline bool enabled() { return detail::descriptor() >= 0; }
// Call first thing in main(): validates and takes over the descriptor before
// any other file is opened.
inline void initialize() { (void)detail::descriptor(); }
using detail::child_label;

// The legacy emitter. Host ops routed through the CP16 Host layer trace via
// nift::rt::host::TraceChannel instead (same descriptor and mutex); the ops
// still traced here are written in the active version's framing: under v2 a
// line carries "v":2 and the v1 result class (no "err"), so one trace file
// never mixes formats.
inline void emit(const char* op, const std::string& arg, const std::string& result) {
    if (!enabled()) return;
    std::string line = detail::version() == 2 ? "{\"v\":2,\"ctx\":\"" : "{\"ctx\":\"";
    detail::escape(line, detail::context());
    line += "\",\"op\":\"";
    line += op;
    line += "\",\"arg\":\"";
    detail::escape(line, arg);
    line += "\",\"res\":\"";
    detail::escape(line, result);
    line += "\"}\n";
    std::lock_guard<std::mutex> lock(detail::write_mutex());
    const char* data = line.data();
    std::size_t left = line.size();
    while (left > 0) {
#ifdef _WIN32
        const int written = ::_write(detail::descriptor(), data, static_cast<unsigned>(left));
#else
        const auto written = ::write(detail::descriptor(), data, left);
#endif
        if (written <= 0) return;
        data += written;
        left -= static_cast<std::size_t>(written);
    }
}

// Sets the trace context for the current thread for the guard's lifetime.
class ContextGuard {
public:
    explicit ContextGuard(const std::string& name) {
        if (!enabled()) return;
        active_ = true;
        previous_ = detail::context();
        detail::context() = name;
    }
    ~ContextGuard() { if (active_) detail::context() = previous_; }
    ContextGuard(const ContextGuard&) = delete;
    ContextGuard& operator=(const ContextGuard&) = delete;
private:
    bool active_ = false;
    std::string previous_;
};

} // namespace nift::trace
