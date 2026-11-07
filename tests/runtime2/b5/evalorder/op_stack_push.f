// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): stack push: receiver first (legacy 0, 1).
st := stack()
keep := st
fn(g()) { st = stack(); return 4 }
st.push(g())
print(st.size())
print(keep.size())
