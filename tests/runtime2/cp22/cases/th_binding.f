fn(w(n)) { return n }
t := thread(w, 1)
y := t
print(y.join())
x := 5
x = t
print("never")
