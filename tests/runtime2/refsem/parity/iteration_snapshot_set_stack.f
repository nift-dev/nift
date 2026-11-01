// D5: sets and stacks iterate the membership at loop entry (legacy-equal).
// The array case moved to refuse/loop_array_push.f (D-NEXT-refsem #1: an
// array mutated while a loop iterates it is refused until Phase B).
s := set()
s.add(1)
s.add(2)
for (x : s) { s.add(x + 10); print(x) }
print(s.size())
st := stack()
st.push(1)
st.push(2)
for (x : st) { st.push(x * 10); print(x) }
print(st.size())
