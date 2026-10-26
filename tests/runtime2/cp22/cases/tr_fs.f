fn[async](work(dn)) {
  mkdir(dn)
  touch(dn + "/f.txt")
  a := is_file(dn + "/f.txt")
  rm(dn + "/f.txt")
  b := is_file(dn + "/f.txt")
  return dn + ":" + a + ":" + b
}
x := work("d1")
y := work("d2")
print(await x)
print(await y)
print(is_dir("d1"))
