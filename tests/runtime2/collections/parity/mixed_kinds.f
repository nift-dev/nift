// B4 (b4-dispatch.md): formerly refuse/ (collection-receiver:size); a bare binding
// that is a map or a stack dispatches `size` at run time (legacy parity: 0).
print("ran")
x := 1
c := map()
if (x > 0) { c = stack() }
print(c.size())
