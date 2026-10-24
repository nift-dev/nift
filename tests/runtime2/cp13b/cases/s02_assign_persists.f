n := 1
arr := [1]
try {
  n = 2
  arr.push(5)
  throw error("x")
} catch(e) {
  print(n)
  print(arr.length())
}
