a := [1,2,3]
f := () => 0
i := 0
while (i < 2) {
  i += 1
  for (x : a) { f(); print(x) }
  f = () => { a = [9]
    return 0 }
}
print("done")
