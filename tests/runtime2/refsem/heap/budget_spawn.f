// D-NEXT-refsem #5 (D7): the cooperative spawn copy reserves its footprint before the task exists.
fn[async](w(a)) { return 1 }
m := map()
i := 0
while (i < 40000) {
    m.set(i, i)
    i += 1
}
print("filled")
f := w(m)
print(await f)
print("done")
