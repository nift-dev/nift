fn(boom()) {
  throw error("boom")
}
fn(f()) {
  return boom()
}
print(f())
