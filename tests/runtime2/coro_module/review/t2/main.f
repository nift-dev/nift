print("pre")
import("counter.f")
b := bump
cl := () => b()
t := thread(cl)
print(t.join())
print(peek())
