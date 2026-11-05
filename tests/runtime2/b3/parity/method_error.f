// B3: an error thrown with the presentation of an instance (private fields skipped)
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
c := C()
print(c.get())
try { throw error(c.stringify()) } catch (e) { print(e.message) }
try { throw error([c].prettify()) } catch (e) { print(e.message) }
