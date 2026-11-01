struct(C) {
  v := 0
}
import("m.f")
print("before")
xs := [C(), map()]
for (x : xs) { print(present_one(x)) }
