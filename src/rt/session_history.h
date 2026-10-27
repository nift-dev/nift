#pragma once
// Compiler-only call-result bounds for retained higher-order bodies.
#include "rt/compiler.h"
#include "rt/session_text.h"
namespace nift::rt {
bool session_staging_covered(const std::shared_ptr<const SessionCallableShape>&);
std::string validate_session_history(const UnitKinds&, std::size_t main);
void prepare_session_history(const Ast&, const SessionText&, const SessionCompileContext&, SessionCompileContext&);
void record_session_history(const Ast&, const UnitKinds&, const SessionCompileContext&, Program&);
void map_session_history(const SessionText&, Program&);
}
