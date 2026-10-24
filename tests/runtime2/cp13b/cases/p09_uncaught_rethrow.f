e := error("orig")
try {
  throw e
} catch(c) {
  print("caught")
  throw c
}
