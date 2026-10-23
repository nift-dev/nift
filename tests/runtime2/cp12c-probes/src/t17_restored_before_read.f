a := [1,2,3]
n := 0
for (x : a) { n += 1; if (n == 2) { a = [5,6,7] }
  print(x); if (n == 1) { a = [1] } }
print("done")
