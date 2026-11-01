// D-NEXT-refsem #1: admitted; a global mutated and lent only by main code,
// which no function references: nothing mutates it during a call.
a := [1]
fn(total(x)) {
  s := 0
  for (v : x) { s += v }
  return s
}
a.push(2)
print(total(a))
a.push(3)
print(total(a))
