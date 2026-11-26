struct(Formatter) {
  fn(combine(args)) { return args }
}
print("before")
f := Formatter()
print(f.combine(4))
