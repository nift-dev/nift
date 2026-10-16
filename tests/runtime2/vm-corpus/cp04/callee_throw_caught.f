fn(bad(n)) { if(n > 1) { throw error("too big") } return n }
try {
  print(bad(1))
  print(bad(5))
} catch(e) {
  print("caught")
}
print("end")
