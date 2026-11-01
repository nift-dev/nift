// D-NEXT-refsem #5 (D7): arguments aliased before the call alias one copy in the task,
// made at task creation (a later mutation by the caller is not seen), in both schedulers.
fn[async](w(a, b)) { return [a, b] }
m := map()
f := w(m, m)
m.set(1, 1)
r := await f
x := r[0]
y := r[1]
print(same(x, y))
print(same(x, m))
print(x.size())
print(m.size())
