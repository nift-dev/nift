#include "rt/host/host_trace.h"
// rt-lint: cold-file parses NIFT_TRACE_HOST_VERSION once per process

#include "HostTrace.h"

#include <cstdlib>
#include <cstring>
#include <mutex>

namespace nift::rt::host {

namespace {

void append_string(std::string& out, const std::string& text) {
    out.push_back('"');
    nift::trace::detail::escape(out, text);
    out.push_back('"');
}

// Writes through the descriptor nift::trace validated and took over, under
// its mutex, so v1 lines from legacy and Host lines never interleave.
class FdSink final : public TraceSink {
public:
    void write_line(const std::string& line) override {
        const int fd = nift::trace::detail::descriptor();
        if (fd < 0) return;
        std::lock_guard<std::mutex> lock(nift::trace::detail::write_mutex());
        const char* data = line.data();
        std::size_t left = line.size();
        while (left > 0) {
#ifdef _WIN32
            const int written = ::_write(fd, data, static_cast<unsigned>(left));
#else
            const auto written = ::write(fd, data, left);
#endif
            if (written <= 0) return;  // dropped silently, as v1
            data += written;
            left -= static_cast<std::size_t>(written);
        }
    }
    std::string context() override { return nift::trace::detail::context(); }
};

// Taken once, with the descriptor (src/HostTrace.h), which also removes the
// variable from the environment.
int take_version() { return nift::trace::detail::version() == 2 ? TraceChannel::V2 : TraceChannel::V1; }

} // namespace

int TraceChannel::parse_version(const char* text) {
    if (!text || !*text || std::strcmp(text, "1") == 0) return V1;
    if (std::strcmp(text, "2") == 0) return V2;
    return -1;
}

TraceChannel& TraceChannel::process_default() {
    static FdSink sink;
    static TraceChannel channel(nift::trace::enabled() ? take_version() : Off, &sink);
    return channel;
}

std::string TraceChannel::format_v1(const std::string& ctx, const char* op, const std::string& arg,
                                    const std::string& res) {
    std::string line = "{\"ctx\":";
    append_string(line, ctx);
    line += ",\"op\":\"";
    line += op;
    line += "\",\"arg\":";
    append_string(line, arg);
    line += ",\"res\":";
    append_string(line, res);
    line += "}\n";
    return line;
}

std::string TraceChannel::format_v2(const std::string& ctx, const TraceV2Record& record) {
    std::string line = "{\"v\":2,\"ctx\":";
    append_string(line, ctx);
    line += ",\"op\":\"";
    line += record.op;
    line += "\",\"arg\":";
    append_string(line, record.arg);
    line += ",\"res\":";
    append_string(line, record.res);
    if (!record.err.empty()) {
        line += ",\"err\":";
        append_string(line, record.err);
    }
    if (record.has_argv) {
        line += ",\"argv\":[";
        for (std::size_t i = 0; i < record.argv.size(); ++i) {
            if (i) line.push_back(',');
            append_string(line, record.argv[i]);
        }
        line.push_back(']');
    }
    if (record.has_cwd) {
        line += ",\"cwd\":";
        append_string(line, record.cwd);
    }
    line += "}\n";
    return line;
}

void TraceChannel::emit_v1(const char* op, const std::string& arg, const std::string& res) {
    if (version_ != V1 || !op) return;
    sink_->write_line(format_v1(sink_->context(), op, arg, res));
}

void TraceChannel::emit_v2(const TraceV2Record& record) {
    if (version_ != V2) return;
    sink_->write_line(format_v2(sink_->context(), record));
}

} // namespace nift::rt::host
