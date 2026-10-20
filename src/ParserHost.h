#pragma once
// CP16: the legacy interpreter's Host operations go through the Host layer
// (src/rt/host). Script built-ins and the observation primitives run each
// operation as a HostCall: the trace (v1 parity lines, or v2) comes from the
// op table, and recording goes through ParserHostObserver, which writes the
// same page observations host-deps defined (D-028). Paths are resolved and
// validated at the legacy call sites (their messages are kept) and adopted
// by the call. See docs/runtime-2/checkpoints/cp16-host-ops-impl.md §9.
#include "PageObserver.h"
#include "rt/host/host_call.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

class Parser;

// The legacy interpreter's PageObserver (PageObserver.h, shared by every
// engine): the parser's RenderHost and RenderResult, observing exactly when
// Parser::observing() says so.
class ParserHostObserver final : public PageObserver {
public:
    explicit ParserHostObserver(const Parser& parser);
};

// The process-wide trace channel while NIFT_TRACE_HOST is enabled, else null.
nift::rt::host::TraceChannel* parser_host_trace();
inline nift::rt::host::HostCallEnv parser_host_env(nift::rt::host::Observer* observer) {
    return nift::rt::host::HostCallEnv{parser_host_trace(), observer, nullptr};
}
// An error for HostCall::fail (the legacy message is set separately).
nift::rt::host::HostError parser_host_error(const char* code, std::string message);
