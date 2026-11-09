// B04 (owner ruling Q7, 2026-11-07, copy-sort-commit; expectations approved 2026-11-09 17:31): a
// comparator that returns a value of the wrong type (not a bool) at its 3rd call fails the sort with
// a fatal error. Up to that call every comparator call sees the receiver and its aliases in the
// original order, element by element (the sort works on a copy; legacy shows the partial sort);
// the error and its position are legacy's.
xs := [9, 2, 7, 4, 6]
al := xs
box := {"r": xs}
n := 0
xs.sort((p, q) => {
  n += 1
  print("call " + n.to_string() + " " + xs[0].to_string() + xs[1].to_string() + xs[2].to_string() + xs[3].to_string() + xs[4].to_string() + " " + al.stringify() + " " + box.r.stringify())
  if (n == 3) { return "yes" }
  return p < q
})
print("unreached")
