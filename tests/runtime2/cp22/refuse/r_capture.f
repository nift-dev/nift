fn(mk()) { n := 1; f := async () => { n = n + 1; return n }; p := f(); n = 10; return [await p, n] }
print(mk().stringify())
