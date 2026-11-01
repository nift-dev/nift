// Review round 3 (coordinator, D-NEXT-refsem): in a program that declares a
// struct, presenting a scalar or string stays admitted (legacy-equal), as do
// equality on arrays holding instances (identity per element), copy and
// deepcopy of them, and reading instances back out of containers.
struct(C) {
  v := 0
}
x := 5
print(x.stringify())
s := "a\"b"
print(s.prettify())
c := C()
print([[c]] == [[c]])
print([C()] == [C()])
print([{"k": c}] != [{"k": c}])
a := [c, C()]
b := copy(a)
d := deepcopy(a)
print(b.length() + d.length())
m := map()
m.set("a", c)
y := m.get("a")
print(y.v)
st := stack()
st.push(C())
z := st.pop()
print(z.v)
