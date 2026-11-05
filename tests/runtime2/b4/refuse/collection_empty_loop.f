// B4 (b4-dispatch.md 2, found by the mixed-receiver generator): legacy's prepared evaluator
// answers false for an empty collection's empty() (true at top level); the VM compiled true.
// Refused before any effect in loop code and in functions called from it.
m := map()
print("before")
for (i : [1]) { print(m.empty()) }
