a := [" x "]
bb := [" y "]
fn(getA()) { return a }
fn(getB()) { return bb }
ua := () => { return a[0] }
ub := () => { return bb[0].trim() }
fn(mix(c)) {
  b := [1]
  if(c == 2) { b = [2] }
  if(c == 3) { b = [3] }
  if(c == 4) { b = [4] }
  if(c == 5) { b = [5] }
  if(c == 6) { b = [6] }
  if(c == 7) { b = [7] }
  if(c == 8) { b = [8] }
  if(c == 9) { b = [9] }
  if(c == 10) { b = [10] }
  if(c == 11) { b = [11] }
  if(c == 12) { b = [12] }
  if(c == 13) { b = [13] }
  if(c == 14) { b = [14] }
  if(c == 15) { b = [15] }
  if(c == 16) { b = [16] }
  if(c == 17) { b = [17] }
  if(c == 18) { b = [18] }
  if(c == 19) { b = [19] }
  m := b
  if(c == 0) { m = getB() }
  m[0] = 5
  k := getB()
  if(c == 0) { k = getA() }
  k[0] = 6
}
print(ua())
print(ub())
mix(0)
print(ua())
print(ub())
