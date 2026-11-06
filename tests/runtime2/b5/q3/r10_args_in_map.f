// Q3 row 10 (§3 args, Q1): extracted from a map, still the immutable args.
m := map()
m.set("a", args)
v := m.get("a")
v.push(1)
print("unreached")
