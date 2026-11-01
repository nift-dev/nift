// HAND-MAINTAINED (not regenerated): derived from rt-diff gen.py --seed 7
// index 2 and edited for D-NEXT-refsem #1: three copies are explicit
// (deepcopy) so the Phase A guard admits it (legacy aliased there, D-040;
// with the copies legacy and the VM agree). The selftest needs only a module
// import (rt_diff_selftest.sh, "module program minimised").
import("mod1.f")
print(mf2((7 * 100)))
a3 := [(7 % 1)]
w4 := 0
while(w4 < 1) {
  al5 := deepcopy(a3)
  al5.push(4)
  print(a3.stringify())
  print(al5.stringify())
  al5 = [0]
  print(a3.stringify())
  print(al5.stringify())
  t6 := "Nift"
  print(t6.length())
  print(t6.to_upper())
  print(t6.contains("a"))
  print(t6.starts_with("N"))
  print(t6.split("-").stringify())
  w4 += 1
}
if(a3.length() > 0) { print(a3[0]) } else { print("empty") }
lc7 := 0
while(true) {
  lc7 += 1
  if(lc7 > 3) { break }
  if(lc7 == 1) { continue }
  print(lc7)
}
for(fe8 : [1, 2, 3, 4]) {
  if(fe8 == 3) { continue }
  if(fe8 == 5) { break }
  print(fe8)
}
fn(mut9(xs)) { xs.push(99); return xs.length() }
a10 := [1, 2]
print(mut9(deepcopy(a10)))
print(a10.stringify())
s11 := "p" + "p"
for(e12 : deepcopy(a3)) {
  print(e12)
  print("item $[e12]")
  a3.push((42 % 3))
}
