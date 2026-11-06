// integ/refsem-a (Phase A x args/cmd): `a := args` then a mutation of `a`.
// Legacy's `a` aliases the invocation binding (`args` grows too); a copy
// model prints a different `args.length()`. Phase B's meaning differs from
// a copy, so the guard refuses it before any effect (weekend/r2 ran it with
// a copy: `args` unchanged, unlike legacy).
print("before")
a := args
a.push("x")
print(a.length())
print(args.length())
