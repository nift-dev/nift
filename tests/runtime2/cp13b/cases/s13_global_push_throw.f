g := [1, 2]
fn(boom()) { throw error("in push arg") }
try {
  g.push(boom())
} catch(e) { print("caught") }
print(g.length())
k := 5
try {
  k += boom().length()
} catch(e) { print("caught2") }
print(k)
