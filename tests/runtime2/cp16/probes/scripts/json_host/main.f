// inject: JSON fast path, Nift-expression fallback, string interpolation, cycles.
print(inject("exact.json").stringify())
print(inject("str.json"))
print(inject("arr.expr").stringify())
p := "exact.json"; print(inject(p).n)
w := file("o.txt"); w.open("w"); w.write_val(inject("exact.json")); w.save(); w.close(); cat("o.txt")
print(inject("self.expr"))
