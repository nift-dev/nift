// Checkpoint 1 (combined-b3-b5.md section 95): object member, nested and compound element stores,
// legacy-equal (no alias, no rhs effect). A plain member store goes only to an object no path goes
// through: the inference then forgets the object's member kinds (known-limitations.md).
o := {"k": 1, "m": {"x": [1, 2]}}
o["m"]["x"][1] = 9
o["m"]["x"][0] += 10
grid := [[1, 2], [3, 4]]
i := 1
grid[i][0] = 7
grid[0][i] *= 3
grid[1][1] -= 1
grid[0][0] /= 4
grid[1][0] %= 4
print(o.stringify())
print(grid.stringify())
n := 0
while (n < 3) {
  j := n % 2
  grid[j][0] += n
  o["m"]["x"][0] -= 1
  n += 1
}
print(grid.stringify())
print(o.stringify())
s := {"n": "s"}
s["n"] += "t"
t := {"n": 0}
t["q"] = 1
t["n"] = 2
print(s.stringify())
print(t.stringify())
