fn(f(x)) { return x * 2 }
i := 0
while(i < 10) {
  i += 1
  if(f(i) > 6) { return i }
  print(i)
}
print("not reached")
