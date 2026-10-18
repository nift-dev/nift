fn(f(n)) {
  if(n > 0) {
    throw error("raised in f")
  }
  return n
}
print(f(0))
print(f(1))
