// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): the argument rebinds b, then throws: nothing is mutated, the rebinding remains.
b := [1, 2]
keep := b
g := () => { b = [7]; throw error("boom") }
try { b.push(g()) } catch (e) { print("caught") }
try { b[0] = g() } catch (e) { print("caught") }
print(b.stringify())
print(keep.stringify())
