fn(work(name)) {
  f := file(name); f.open("w"); f.write("t"); f.save(); f.close()
  e := is_file(name)
  rm(name)
  return e
}
t1 := thread(work, "t1.txt")
t2 := thread(work, "t2.txt")
print(t1.join())
print(t2.join())
