#pragma once
// CP16 Host layer: Host-op trace channel, v1 (parity) and v2 (O2, adopted).
//
// v1 is legacy's format (docs/runtime-2/trace-format.md), written through the
// shared nift::trace emitter so descriptor handling, context labels and
// escaping are not reimplemented.
//
// v2 (NIFT_TRACE_HOST_VERSION=2 together with NIFT_TRACE_HOST=<fd>) writes
// one line per Host op, including the ops v1 does not trace, after the
// attempt, with the outcome:
//
//   {"v":2,"ctx":"script","op":"save","arg":"/abs/x","res":"error","err":"io.write_failed"}
//   {"v":2,"ctx":"script","op":"spawn","arg":"sh","res":"launched","argv":["sh","-c","x"],"cwd":"/abs"}
//
// `res` on success is the op's result class (v1's where v1 has one: stat
// kind, byte count, open mode, found/unset; otherwise "ok"); on failure it is
// "error" and `err` carries the error code (severity and catchability follow
// from the code table, host_error.h). A process launch failure is a value,
// not an error: `res` "launch_failed", no `err`. Only one version is active
// per process; v1 stays the parity channel until legacy also emits v2.
#include <string>
#include <vector>

namespace nift::rt::host {

class TraceSink {
public:
    virtual ~TraceSink() = default;
    virtual void write_line(const std::string& line) = 0;  // one complete line, '\n' included
    virtual std::string context() = 0;                     // "script", page name, "<owner>/async#N"
};

struct TraceV2Record {
    const char* op = "";
    std::string arg;
    std::string res;
    std::string err;                // empty on success
    bool has_argv = false;
    std::vector<std::string> argv;  // spawn: JSON array (Q-H11: unambiguous)
    bool has_cwd = false;
    std::string cwd;                // spawn: absolute working directory
};

class TraceChannel {
public:
    enum Version : int { Off = 0, V1 = 1, V2 = 2 };
    TraceChannel(int version, TraceSink* sink) : version_(sink ? version : Off), sink_(sink) {}
    // The process-wide channel: NIFT_TRACE_HOST (validated by src/HostTrace.h)
    // and NIFT_TRACE_HOST_VERSION, both read once; the version variable is
    // removed from the environment like NIFT_TRACE_HOST, so scripts and
    // children see the same environment with and without tracing.
    static TraceChannel& process_default();
    // Parses NIFT_TRACE_HOST_VERSION text: "2" selects v2; unset, empty or
    // "1" selects v1; anything else is invalid (returns -1).
    static int parse_version(const char* text);

    int version() const { return version_; }
    bool v1() const { return version_ == V1; }
    bool v2() const { return version_ == V2; }
    void emit_v1(const char* op, const std::string& arg, const std::string& res);
    void emit_v2(const TraceV2Record& record);

    static std::string format_v1(const std::string& ctx, const char* op, const std::string& arg, const std::string& res);
    static std::string format_v2(const std::string& ctx, const TraceV2Record& record);

private:
    int version_;
    TraceSink* sink_;
};

} // namespace nift::rt::host
