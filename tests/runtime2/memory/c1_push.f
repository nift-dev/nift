a := []
i := 0
while (i < 400000000) {
  a.push("abcdefghijklmnopqrstuvwxyz-" + i)
  i += 1
}
print(a.size())
