// B4 (owner): interpolating a map alone is an ordinary run-time error (formerly refused,
// interpolation:unknown-type); legacy prints its internal tag (golden .vm).
m := map()
m.set("k", 1)
print("before")
print("v=$[m]")
