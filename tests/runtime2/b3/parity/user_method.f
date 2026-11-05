// B3 approved change: a struct's own stringify/prettify/highlight method does
// not shadow the presentation built-in (legacy prints C{v:0}; the VM used to
// call the method and print 7).
struct(C) {
  v := 0
  private w := "SECRET-b3"
  fn(stringify()) { return 7 }
  fn(prettify()) { return 8 }
  fn(highlight()) { return 9 }
}
c := C()
print(c.stringify())
print(c.prettify())
print(c.highlight())
