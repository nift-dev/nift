fn(bad()) {
  return undefined_name
}
try {
  bad()
} catch(e) {
  print(e.message)
}
