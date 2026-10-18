fn(f()) {
  print("in f")
  return nosuch
}
try {
  f()
} catch(e) {
  print("caught")
}
