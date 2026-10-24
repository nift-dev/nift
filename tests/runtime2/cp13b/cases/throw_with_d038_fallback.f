fn(fetch()) {
  return zz
}
fn(caller()) {
  zz := 5
  try {
    return fetch()
  } catch(e) {
    return 0
  }
}
print(caller())
