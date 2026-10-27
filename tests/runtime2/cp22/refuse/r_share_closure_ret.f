fn[async](mk(start)) { n := start; f := () => { n += 1; return n }; return f }
a := await mk(10)
b := await mk(20)
print(a() + b())
print(a())
