fn(id(q)) { return q }
fn(id2(p, q)) { return q }
fn(loud(x)) { print("loud")
return x }
fn(mk()) { return [1, 2, 3] }
fn(c1(x)) { if (x == 0) { return 1 / x }
return c1(x - 1) + 1 }
fn(c2(x)) { return id(10 / x) }
fn(c3(a, i)) { return a[i] }
fn(c4(x)) { y := x
y = y + 0.5
return y }
fn(c5(x)) { k := 0
while (k < x) { k += 1 }
return 10 / k }
fn(c6(d)) { if (d == 0) { return 0 }
return c6(d - 1) + 1 }
fn(c7(v)) { return v.length() }
fn(w0()) {
z := 0
h := 5.5
arr := [1, 2]
o := {"k": 1, "s": "t"}
big := 2000000000 * 2000000000
k5 := 5
neg := -1
fr := 0.5
two := 2.0
n := 7
g := 2.5
w := 9007199254740991
v2 := 0
ks := "zz"
t := 0
t = loud(w) * 2
print(t)
return 7
}
print("before")
i0 := 0
while (i0 < 1) { i0 += 1
print(w0()) }
print("after")
