// B4b review H1: the same through a function returning the inexact callee.
fn(L(a)) { return a.trim() }
print(L(" a "))
mx := mutex(1)
fn(pick() ) { return L(" b ") == "b" ? L : mx.get() }
t := thread(pick(), 5)
print(t.join())
