fn(mutate(v)) { v[1] = 9 }
a := [1,2,3]
for (x : a) { print(x); mutate(a) }
