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
// Tracing never changes any other output.
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace nift::trace {

namespace detail {
inline int descriptor_from_environment() {
    const char* value = std::getenv("NIFT_TRACE_HOST");
    if (!value || !*value) return -1;
    char* end = nullptr;
    const long fd = std::strtol(value, &end, 10);
    if (!end || *end != '\0' || fd < 0 || fd > 1 << 20) return -1;
    return static_cast<int>(fd);
}
inline int descriptor() {
    static const int fd = descriptor_from_environment();
    return fd;
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

inline void emit(const char* op, const std::string& arg, const std::string& result) {
    if (!enabled()) return;
    std::string line = "{\"ctx\":\"";
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
