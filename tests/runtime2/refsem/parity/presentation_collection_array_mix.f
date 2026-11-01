// Review round 3 (D-NEXT-refsem), review repro inf/j01.f: presentation over a
// map/array/set mix (no struct) stays admitted and matches legacy.
xs := [map(), [1], set()]
for (x : xs) { s := x.stringify(); print(s.length()); print(s) }
