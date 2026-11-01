// D-NEXT-refsem (sound inference): a store through a parameter reaches the caller's map.
fn(fill(t)) { t.set(1, set()); return 0 }
print("start")
m := map()
fill(m)
x := m.get(1)
print(html_escape(x))
