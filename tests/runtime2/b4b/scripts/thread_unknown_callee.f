// B4b review H1: thread(f, ...) with f of inexact targets (a ternary with a mutex value):
// every function used as a value has parameters typed as anything; L's trim is checked.
// Was exit 70 (unchecked dispatch on an int) in the production binary.
fn(L(a)) { return a.trim() }
print(L(" a "))
mx := mutex(1)
t := thread(L(" b ") == "b" ? L : mx.get(), 5)
print(t.join())
