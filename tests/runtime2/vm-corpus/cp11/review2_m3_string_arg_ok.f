fn(greet(s)) {
  s = s + "!"
  return s
}
print(greet("world"))
i := 0
while (i < 2) { print(greet("x")); i += 1 }
