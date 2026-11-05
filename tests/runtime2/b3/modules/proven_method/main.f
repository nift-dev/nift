// B3 (review H1): a proven instance of the module's struct runs its own
// stringify when called explicitly; rendering it inside an array does not.
// Legacy cannot construct the module's struct through the exported function
// (the pre-existing difference, compatibility.md), so this is a VM golden.
import("m.f")
print("before")
a := make()
b := [a]
print(b.stringify())
print(a.stringify())
