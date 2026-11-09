// B04 (owner ruling Q7, 2026-11-07, copy-sort-commit; expectations approved 2026-11-09 17:31): a
// comparator whose body fails at run time (division by zero) at its 3rd call ends the program with
// legacy's error and position. Up to that call every comparator call sees the receiver and its
// aliases in the original order, element by element (legacy shows the partial sort).
xs := [9, 2, 7, 4, 6]
al := xs
arr := [xs]
n := 0
xs.sort((p, q) => {
  n += 1
  print("call " + n.to_string() + " " + xs[0].to_string() + xs[1].to_string() + xs[2].to_string() + xs[3].to_string() + xs[4].to_string() + " " + al.stringify() + " " + arr[0].stringify())
  return p / (3 - n) < q
})
print("unreached")
