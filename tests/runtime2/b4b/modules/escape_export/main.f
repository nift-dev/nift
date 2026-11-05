// B4b review H1 (owner: module boundaries): a module's exported function reached by the
// importer through a value of inexact targets (a thread entry): its parameters are anything.
import("m.f")
mx := mutex(1)
fn(pick()) { return L(" b ") == "b" ? L : mx.get() }
t := thread(pick(), 5)
print(t.join())
