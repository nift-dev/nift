// B4: an explicit stringify() on a receiver that may be an instance of C (declaring its own
// stringify), of D (declaring none), or an array: C's method for C's instance, the built-in
// for the others (StructIs tests the instance's own descriptor; mutant A7 ignores it).
// Legacy ignores the method: the owner's explicit-call ruling (golden .vm).
struct(C) {
  a := 1
  fn(stringify()) { print("SIDE-EFFECT C"); return "user" }
}
struct(D) { b := 2 }
xs := [C(), D(), [1]]
print("before")
for (v : xs) { print(v.stringify()) }
