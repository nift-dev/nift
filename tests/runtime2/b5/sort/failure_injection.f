// B5 (owner Q7 ruling): deterministic failure injection. The comparator throws at its k-th call,
// for every k the sort makes; each time the receiver (and every alias) is unchanged.
total := 0
probe := [9, 2, 7, 4, 6, 1, 8, 3, 5]
probe.sort((p, q) => { total += 1; return p < q })
fn(attempt(k)) {
  xs := [9, 2, 7, 4, 6, 1, 8, 3, 5]
  al := xs
  n := 0
  try {
    xs.sort((p, q) => { n += 1; if (n == k) { throw error("stop") }; return p < q })
  } catch (e) {
    if (al.stringify() != "[9,2,7,4,6,1,8,3,5]") { return 1 }
    return 0
  }
  return 100
}
fn(each(k)) {
  if (k > total) { return 0 }
  return attempt(k) + each(k + 1)
}
print(total > 5)
print(each(1))
print("done")
