// B4 (b4-dispatch.md 2, found by the mixed-receiver generator): a fraction's to_int() error in
// loop code is reported by legacy at the statement, by the VM at the call: refused before any
// effect (it compiled before B4 for a number-only receiver and printed another position).
print("before")
for (v : [5, 2.5]) { print(v.to_int()) }
