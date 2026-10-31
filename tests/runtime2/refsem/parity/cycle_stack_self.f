// D-NEXT-refsem #4: a stack pushed onto itself is refused (legacy text).
st := stack()
print("before")
st.push(st)
