a := [1, 2, 3]
i := 2
a[i] = 9
a[0] = a[i] + 1
j := 0
while (j < 2) {
  a[j] = a[j] * 2
  j += 1
}
print(a[0])
print(a[1])
print(a[2])
