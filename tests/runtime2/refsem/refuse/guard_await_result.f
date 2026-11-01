// D-NEXT-refsem #1 (D2 guard): an awaited array (the future keeps it) mutated.
fn[async](w()) { return [1] }
print("start")
f := w()
x := await f
x.push(2)
y := await f
print(y.stringify())
