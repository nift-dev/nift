// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): the argument rebinds b and returns the original: pushing it into itself is a refused cycle (catchable); the original is unchanged and the rebinding remains (before the repair the push went into the new array and succeeded).
b := [1]
keep := b
g := () => { b = [7]; return keep }
try { b.push(g()) } catch (e) { print("caught") }
print(b.stringify())
print(keep.stringify())
