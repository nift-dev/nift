i := 0
while(i < 10) {
  i += 1
  if(i % 2 == 0) { continue }
  if(i > 7) { break }
  print(i)
}
n := 5
if(n < 3) { print("small") } else if(n < 6) { print("medium") } else { print("large") }
if(n == 5) { print("five") }
if(n != 5) { print("not five") } else { print("else") }
k := 1
if(true) { k := 2; print(k) }
print(k)
j := 0
total := 0
while(j < 4) {
  m := j * 10
  total += m
  inner := 0
  while(inner < 3) { inner += 1; if(inner == 2) { continue } total += inner }
  j += 1
}
print(total)
while(false) { print("never") }
c := 0
while(c < 100) { c += 7 }
print(c)
