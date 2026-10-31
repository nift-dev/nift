// D-NEXT-refsem #6 (D6): struct identity keys stay refused (not established on the VM).
struct(P) { v := 0 }
print("start")
p := P()
s := set()
s.add(p)
