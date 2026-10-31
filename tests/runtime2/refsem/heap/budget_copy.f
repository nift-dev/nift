// D-NEXT-refsem: the copy reserves its whole footprint before building anything.
m := map()
i := 0
while (i < 40000) {
    m.set(i, i)
    i += 1
}
print("filled")
n := 0
d := copy(m)
print("done")
