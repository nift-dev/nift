// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): an argument rebinds the receiver; the push goes to the array b held (legacy [7]).
b := [1]
g := () => { b = [7]; return 4 }
b.push(g())
print(b.stringify())
