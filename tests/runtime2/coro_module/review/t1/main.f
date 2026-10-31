print("pre")
import("counter.f")
a := bump
b := a
c := b
t := thread(c)
print(t.join())
print(peek())
