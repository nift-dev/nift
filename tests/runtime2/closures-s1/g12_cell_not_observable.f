fn(make(x)) {
  return () => x
}
f := make(7)
print(type(f()))
