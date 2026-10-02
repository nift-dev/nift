#include "../src/Outcome.h"

#include <cassert>
#include <cstdint>
#include <set>
#include <string>

int main() {
    using nift::detail::DiagnosticCode;
    using nift::detail::DiagnosticDisposition;
    using nift::detail::DiagnosticFrameKind;
    using nift::detail::EvalOutcome;
    using nift::detail::WorkerCompletion;

    const auto syntax = nift::detail::diagnostic_code_info(DiagnosticCode::NativeSyntaxError);
    assert(syntax.text == "native.syntax_error");
    assert(syntax.disposition == DiagnosticDisposition::Fatal);

    const auto io = nift::detail::diagnostic_code_info(DiagnosticCode::IoReadFailed);
    assert(io.text == "io.read_failed");
    assert(io.disposition == DiagnosticDisposition::Recoverable);

    std::set<std::string> registered_codes;
    std::uint64_t registry_fingerprint = 1469598103934665603ULL;
    for (int value = 0; value < static_cast<int>(DiagnosticCode::Count); ++value) {
        const auto info = nift::detail::diagnostic_code_info(
            static_cast<DiagnosticCode>(value));
        assert(!info.text.empty());
        assert(registered_codes.emplace(info.text).second);
        for (const unsigned char byte : info.text) {
            registry_fingerprint ^= byte;
            registry_fingerprint *= 1099511628211ULL;
        }
        registry_fingerprint ^= static_cast<unsigned>(info.disposition);
        registry_fingerprint *= 1099511628211ULL;
    }
    assert(static_cast<int>(DiagnosticCode::Count) == 61);
    assert(registry_fingerprint == 12391608587057904430ULL);
    assert(registered_codes.count("user.raised") == 1);
    assert(registered_codes.count("internal.host_exception") == 1);
    assert(registered_codes.count("host.callable_exception") == 0);
    assert(registered_codes.count("ffi.library_unload_failed") == 0);

    auto diagnostic = nift::detail::make_diagnostic(
        DiagnosticCode::InternalLegacyFailure, "division by zero");
    diagnostic.frames.push_back(
        {DiagnosticFrameKind::Callable, "calculate", {}, "callable: "});
    diagnostic.frames.push_back(
        {DiagnosticFrameKind::Future, "await", {}, "future: "});
    assert(nift::detail::project_diagnostic(diagnostic) ==
           "future: callable: division by zero");

    auto unsupported = EvalOutcome<int>::unsupported();
    assert(unsupported.kind() == EvalOutcome<int>::Kind::Unsupported);
    auto value = EvalOutcome<int>::value(42);
    assert(value.kind() == EvalOutcome<int>::Kind::Value && value.value() == 42);
    auto failure = EvalOutcome<int>::fatal(diagnostic);
    assert(failure.kind() == EvalOutcome<int>::Kind::Fatal);
    assert(failure.diagnostic().message == "division by zero");

    WorkerCompletion<int> pending;
    assert(pending.pending());
    auto completed = WorkerCompletion<int>::value(7);
    assert(completed.kind() == WorkerCompletion<int>::Kind::Value);
    assert(completed.value() == 7);
    auto fatal = WorkerCompletion<int>::fatal(std::move(diagnostic));
    assert(fatal.kind() == WorkerCompletion<int>::Kind::Fatal);
    assert(nift::detail::project_diagnostic(fatal.diagnostic()) ==
           "future: callable: division by zero");
    assert(nift::detail::project_diagnostic(fatal.diagnostic()) ==
           "future: callable: division by zero");
}
