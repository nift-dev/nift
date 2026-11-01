// D-NEXT-refsem #5 (D7): thread() follows the same rule in both schedulers.
fn(w(a, b)) { return [a, b] }
m := map()
t := thread(w, m, m)
m.set(1, 1)
r := t.join()
x := r[0]
y := r[1]
print(same(x, y))
print(same(x, m))
print(x.size())
