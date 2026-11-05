// B4 (owner): an atomic interpolates as its current value (one sequentially consistent
// load through the runtime's atomic access, as print does): legacy parity, alone and
// mixed with an int or a string.
a := atomic<int>(5)
b := atomic<bool>(true)
print("before")
print("x$[a] $[b]")
for (v : [1, a]) { print("y$[v]") }
for (v : ["s", b]) { print("z$[v]") }
a.store(6)
print("x$[a]")
