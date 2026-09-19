#pragma once
#include <string>
#include <vector>
struct NiftOperationResult { bool ok=true; int exit_code=0; std::vector<std::string> affected; std::vector<std::string> errors; double duration_seconds=0.0; };
struct NiftBuildRequest { enum class Mode { Updated, All, Names, Auto, Repair }; Mode mode=Mode::Updated; std::vector<std::string> names; bool explain=false; };
// v4.4 boundary contract: CLI presentation and script bindings must delegate to
// ProjectInfo through this structured request/result layer. DeepSeek review is
// to finish the extraction without duplicating build semantics.
