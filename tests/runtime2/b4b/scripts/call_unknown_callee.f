// B4b review H1: a direct call through a value of inexact targets.
fn(L(a)) { return a.trim() }
print(L(" a "))
mx := mutex(1)
fn(pick()) { return L(" b ") == "b" ? L : mx.get() }
f := pick()
print(f(5))
