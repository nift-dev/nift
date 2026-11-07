// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): a captured parameter shares the caller's array (approved Phase B argument sharing) and keeps it as the receiver after the closure rebinds the parameter: t returns [7], the caller's array gets the push (legacy [7], [1]: legacy copies arguments).
fn(t(p)) {
  g := () => { p = [7]; return 4 }
  p.push(g())
  return p
}
b := [1]
print(t(b).stringify())
print(b.stringify())
