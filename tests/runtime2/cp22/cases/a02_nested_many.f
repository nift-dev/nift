fn[async](inner(x)) { return x + 1 }
fn[async](outer(x)) { return await inner(x) }
a := outer(1)
b := outer(2)
c := outer(3)
d := outer(4)
ra := await a
rb := await b
rc := await c
rd := await d
print(ra + rb + rc + rd)
