fn[async](bad(n)) { if (n > 1) { throw error("big " + n, "user.big") }; return n }
s := 0
for(i : [1, 2, 3]) {
  try { s += await bad(i) } catch(e) { print(e.message) }
}
print(s)
