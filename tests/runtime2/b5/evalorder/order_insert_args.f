// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): insert evaluates its arguments left to right (legacy i, v, [5,0]).
fn(t(s, v)) { print(s); return v }
b := [0]
b.insert(t("i", 0), t("v", 5))
print(b.stringify())
m := map()
m.set(t("k", "a"), t("w", 1))
print(m.size())
