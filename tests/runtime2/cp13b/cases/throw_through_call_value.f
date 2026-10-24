fn(callit(fail)) {
  try {
    fail("deep")
  } catch(e) {
    print("caught " + e.message)
  }
}
f := (m) => { throw error(m) }
callit(f)
print("end")
