@fn[async](probe(name)) {
  f := file(name); f.open("w"); f.write(name); f.save(); f.close()
  a := exists(name)
  rm(name)
  b := exists(name)
  return name + ":" + a + ":" + b
}
x := probe("a.txt")
y := probe("b.txt")
print(await x)
print(await y)
