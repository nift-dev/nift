a := {"k": " x "}
fn(getA()) { return a }
fn(use()) { print(a.k.trim()) }
fn(mix(c)) {
  b := {"k": 1}
  if(c == 2) { b = {"k": 2} }
  if(c == 3) { b = {"k": 3} }
  if(c == 4) { b = {"k": 4} }
  if(c == 5) { b = {"k": 5} }
  if(c == 6) { b = {"k": 6} }
  if(c == 7) { b = {"k": 7} }
  if(c == 8) { b = {"k": 8} }
  if(c == 9) { b = {"k": 9} }
  if(c == 10) { b = {"k": 10} }
  if(c == 11) { b = {"k": 11} }
  if(c == 12) { b = {"k": 12} }
  if(c == 13) { b = {"k": 13} }
  if(c == 14) { b = {"k": 14} }
  if(c == 15) { b = {"k": 15} }
  if(c == 16) { b = {"k": 16} }
  if(c == 17) { b = {"k": 17} }
  if(c == 18) { b = {"k": 18} }
  if(c == 19) { b = {"k": 19} }
  m := b
  if(c == 0) { m = getA() }
  m["k"] = 5
}
use()
mix(0)
use()
