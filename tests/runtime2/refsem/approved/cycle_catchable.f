// D-NEXT-refsem #4 (D1): a refused cycle is a catchable runtime.reference_cycle Error (legacy: fatal).
m := map()
try {
    m.set("self", m)
} catch (e) {
    print(e.code)
    print(e.message)
}
print(m.size())
st := stack()
try { st.push(st) } catch (e) { print(e.message) }
print(st.size())
