i := 0
a := [1]
while (i < 2) {
  try {
    print(a[i])
  } catch(e) {
    print("caught")
  }
  i += 1
}
