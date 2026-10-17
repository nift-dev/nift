fn(gcd(a, b)) { while(b != 0) { t := b; b = a % b; a = t } return a }
a := 2.5
i := 0
while (i < 2) {
  i += 1
  b := a
  b = 1.5
  print(b)
}
print(gcd(1071, 462))
