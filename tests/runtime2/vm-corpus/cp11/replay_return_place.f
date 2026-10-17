fn(f()) { a := [1]
  return a }
x := f()
x.push(2)
print(x.length())
