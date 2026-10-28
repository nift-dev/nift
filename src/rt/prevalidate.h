#pragma once
// Runtime 2.0 CP13: syntax prevalidation (D-022 #3 and refinements).
//
// A compilation unit (script file, -e program, eval expression, an imported
// module when it is loaded) is validated as a whole before any instruction
// runs, on both engines. The validator is the CP10 parser (one grammar):
//
//  * invalid syntax -> rejected (non-zero exit, nothing has run):
//      - a statement-structure error of the translator (ParseError; legacy
//        reports these before running, identical message and position);
//      - a translator error inside a deferred region (function, method or
//        lambda block body), which legacy reports only when the region runs;
//      - an Opaque(LegacyRuntimeError) region whose own text is definitively
//        malformed by one of the rules below (each verified against legacy:
//        legacy fails, or silently drops the rest of the unit, whenever the
//        statement executes; tests/runtime2/prevalidate/rules.tsv).
//  * valid but VM-unsupported (every other Opaque) -> accepted; the unit
//    runs on legacy exactly as before. The VM's Opaque/fallback semantics are
//    not changed by this file.
//  * valid, compiled -> accepted.
//
// Passing guarantees only valid syntax, not static validity, with one
// static rule: a for-loop or catch binding with a reserved built-in name may
// not be called in its scope (rt/binding_rules.h, rule `binding-call-reserved`).
#include <cstdint>
#include <string>
#include <string_view>

namespace nift::rt {

struct Prevalidation {
    bool ok = true;
    std::uint32_t offset = 0;  // byte offset of the reported position
    std::string message;
    const char* code = "";     // DiagnosticCode text (syntax.invalid_statement / syntax.malformed_expression)
    const char* rule = "";     // which check rejected it (tests and the gate)
};

// Validates a whole unit. Reports the first rejection in source order.
Prevalidation prevalidate(std::string_view source);

} // namespace nift::rt
