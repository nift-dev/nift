#pragma once
// Human-readable text for VM refusals (VM-default policy): the user-facing
// diagnostic printed when a program is not compiled for the VM under the
// default policy or NIFT_VM=require. The stable identifier stays the refusal
// reason string (`method:run`, `opaque:template-text`, ...), which the stats
// line and the classification tooling keep using unchanged.
#include <string>

namespace nift::rt {

// A short description of the refused feature, e.g. "the `.run()` method" for
// `method:run`. Unknown reasons get a generic description; never empty.
std::string describe_refusal(const std::string& reason);

// The invocation as the user typed it, recorded once by the CLI entry point,
// for the legacy instruction `NIFT_VM=0 nift <arguments>`. Arguments are
// shell-quoted where needed; the program name is always spelled `nift`.
void set_invocation_arguments(int argc, char** argv);

// `NIFT_VM=0 nift <recorded arguments>`, or `NIFT_VM=0 nift <fallback>` when
// no invocation was recorded (embedders, unit tests).
std::string legacy_command_hint(const std::string& fallback);

// The complete multi-line diagnostic for a refused unit: location, feature
// description, stable identifier, not-executed statement, legacy instruction
// and issue destination. `where` is `path:line:column`.
std::string vm_refusal_diagnostic(const std::string& where, const std::string& reason, const std::string& fallback);

} // namespace nift::rt
