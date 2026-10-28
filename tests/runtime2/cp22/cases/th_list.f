fn(w(x)) { return x * 2 }
t := thread(w, 2)
print(t.join() + t.join())
ts := [thread(w, 1), thread(w, 2), thread(w, 3)]
ts.push(thread(w, 4))
s := 0
for (h : ts) { s += h.join() }
print(s)
print(ts[0].join())
print(ts.size())
k := 0
while (k < 3) { s += thread(w, k).join(); k += 1 }
print(s)
