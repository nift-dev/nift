// B4 C1: legacy prints the earlier output and the module's before reporting the
// collision; the VM rejects the program before running it (the invocation-export precedent).
print("first")
mf := 1
import("m.f")
print(mf)
