// integ/refsem-a (Phase A x args/cmd): reading `args` (a function argument,
// a loop, an element stored in a map, `cmd`) stays admitted, legacy-equal.
fn(count(xs)) { return xs.length() }
print(count(args))
for (x : args) { print(x) }
m := map()
m.set("n", args.length())
print(m.get("n"))
print(cmd.length() > 0)
