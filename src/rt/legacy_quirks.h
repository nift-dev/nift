#pragma once
// Legacy behaviours the Runtime 2.0 VM reproduces although they are
// candidates for deliberate change (CP12; escalated to the owner, D-014
// class 2/3). Each is value-dependent, so it cannot be refused before
// execution without refusing far more (every loop assignment containing a
// call, for Q1). Each is isolated behind one switch: the compiler part lives
// in rt/compiler_quirks.cpp, the VM part is marked "LEGACY QUIRK Qn" in
// rt/vm_ops.cpp / rt/vm.cpp. Setting a switch to false gives the plain
// semantics described below (then the VM differs from legacy for these
// programs: the parity suites would need the matching approved difference).
namespace nift::rt {

// Q1. A prepared (loop-path) declaration or assignment whose value is a
// large number (|n| >= 2^53 or an exact big number), and a prepared compound
// assignment or increment with such an operand, is re-evaluated by legacy on
// its textual path (ParserTemplate.cpp large_num -> c.legacy(st.text)): the
// statement runs twice, calls in it included (their output appears twice),
// and the textual value is kept. The VM compiles the statement's textual code
// as a cold block entered by JmpLarge / StopReason::Reeval.
// Off: the prepared value is kept and nothing runs twice.
inline constexpr bool kQuirkLargeReevaluation = true;

// Q2. print(path) on the prepared path with a missing location (index past
// the end, missing key or member): legacy resolves the statement again on its
// textual path (location failure), evaluating the keys a second time and
// reporting the textual message. The VM compiles the textual print as a cold
// block entered from the lookup (kInstrPathText, StopReason::Reeval).
// Off: the prepared lookup reports its own error ("invalid index", ...).
inline constexpr bool kQuirkPrintPathReresolve = true;

// Q3. An error with no failing expression (binding type change, compound
// operation, element assignment) on the prepared path is reported at the
// statement of the outermost loop body that contains it, and inside a
// function legacy runs prepared at the call in its caller.
// Off: reported at the failing statement itself.
inline constexpr bool kQuirkStatementErrorSites = true;

} // namespace nift::rt
