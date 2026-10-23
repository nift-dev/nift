fn(make()) {
  a := () => 0
  setter := () => { a = () => a
    return 1 }
  return setter()
}
print(make())
