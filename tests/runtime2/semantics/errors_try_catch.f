try { throw error("nope") } catch(e) {
  print(e.message)
  print(e.code)
}
try { undefined_fn(1) } catch(e3) { print(e3.message) }
print("end")
