i := 0
while (i < 3) {
  i += 1
  if (i == 2) { print("b"); break; print("x") }
  if (i > 5) { break }
}
print(i)
j := 0
while (j < 3) { j += 1; break; print("after") }
print(j)
