struct(box) {
  v := 0
  fn(get()) { return v + undefined_name }
}
b := box()
print(b.get())
