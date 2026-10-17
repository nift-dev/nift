fn(grow(a)) { a.push(99); return a.length() }
a := [1, 2, 3]
print(a[0] + a[2])
a.push(4)
print(a.length())
print(a.size())
a[1] = 20
print(a[1])
print(a[1.0])
print(grow(a))
print(a.length())
b := []
q := 0
while(q < 5) { b.push(q * q); q += 1 }
print(b[4])
print(b.length())
nested := [[1, 2], [3, 4]]
print(nested[1][0])
e := []
print(e.length())
words := ["x", "y"]
words.push("z")
print(words[2])
s := 0
i := 0
while(i < b.length()) { s += b[i]; i += 1 }
print(s)
three := [1, 2, 3]
print(three[1] == 2)
