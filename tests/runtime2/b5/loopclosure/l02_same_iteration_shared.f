// B5 D1 (owner 2026-11-07 06:55, decision 1): same-iteration closures share their binding (a setter's write is seen by that iteration's getter); different iterations are independent; all run after the loop
gets := []
sets := []
for (x : [10, 20]) {
  gets.push(() => x)
  sets.push((v) => {
    x = v
    return 0
  })
}
g0 := gets[0]
g1 := gets[1]
s0 := sets[0]
s1 := sets[1]
s0(11)
print(g0())
print(g1())
s1(21)
print(g0())
print(g1())
