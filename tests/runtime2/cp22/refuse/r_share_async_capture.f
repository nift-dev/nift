fn(go()) {
  n := 0
  inc := async () => { n += 1; return n }
  a := inc()
  b := inc()
  return (await a) + (await b) + n
}
print(go())
