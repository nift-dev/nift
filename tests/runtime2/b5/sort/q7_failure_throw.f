// B04 (owner ruling Q7, 2026-11-07, copy-sort-commit; expectations approved 2026-11-09 17:31): a
// comparator sort that fails by a throw at its k-th call (caught) leaves the receiver and every
// alias unchanged, element by element, for every k the sort makes: a direct alias, an alias held in
// an object field, one held in an array. Identity is kept: a push through the receiver afterwards
// is seen through every alias. (Legacy leaves a permutation in the receiver and its direct alias.)
total := 0
probe := [9, 2, 7, 4, 6, 1, 8, 3, 5]
probe.sort((p, q) => { total += 1; return p < q })
fn(attempt(k)) {
  xs := [9, 2, 7, 4, 6, 1, 8, 3, 5]
  al := xs
  box := {"r": xs}
  arr := [xs]
  n := 0
  try {
    xs.sort((p, q) => { n += 1; if (n == k) { throw error("stop") }; return p < q })
  } catch (e) {
    bad := 0
    if (e.message != "stop") { bad += 1 }
    for (ys : [xs, al, box.r, arr[0]]) {
      if (ys.length() != 9) { bad += 1 }
      if (ys[0] != 9 || ys[1] != 2 || ys[2] != 7 || ys[3] != 4 || ys[4] != 6) { bad += 1 }
      if (ys[5] != 1 || ys[6] != 8 || ys[7] != 3 || ys[8] != 5) { bad += 1 }
    }
    xs.push(0)
    if (al.length() != 10 || box.r.length() != 10 || arr[0].length() != 10) { bad += 1 }
    return bad
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
