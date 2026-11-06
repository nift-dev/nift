// B5 D1 (owner 2026-11-07 06:55, decision 1): closures escape the loop and the function, held in objects inside an array; get/set pairs per iteration
fn(make()) {
  out := []
  for (x : [1, 2, 3]) {
    out.push({"get": () => x, "set": (v) => {
      x = v
      return 0
    }})
  }
  return out
}
cs := make()
c0 := cs[0]
c2 := cs[2]
g0 := c0["get"]
s0 := c0["set"]
g2 := c2["get"]
s2 := c2["set"]
print(g0())
print(g2())
s0(7)
print(g0())
print(g2())
s2(9)
print(g0())
print(g2())
