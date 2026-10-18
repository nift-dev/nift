fn(f()) {
  try {
    return 1
  } catch(e) {
    return 2
  }
  return 3
}
print(f())
