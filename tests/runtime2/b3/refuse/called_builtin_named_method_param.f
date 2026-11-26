struct(Formatter) {
  fn(combine(print)) { return print(5) }
}
print("before")
f := Formatter()
f.combine(4)
