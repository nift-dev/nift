fn(f(a)) { b := 1
  return a / (b - 1) }
print("before")
i := 0
while(i < 3) { print(i); i += 1 }
print(f(1))
print("after")
