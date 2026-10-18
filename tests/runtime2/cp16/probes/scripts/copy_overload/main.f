// copy(v) deep-copies a value; copy(src, dst) copies files (arity decides).
x := {"k": [1]}; y := copy(x); print(y.stringify())
copy("a.txt", "b.txt"); print(open("b.txt"))
s := "a.txt"; print(copy(s))
