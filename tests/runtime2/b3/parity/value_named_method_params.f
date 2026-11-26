struct(Formatter) {
  fn(combine(output, build)) { return output + build }
}
f := Formatter()
print(f.combine(4, 5))
