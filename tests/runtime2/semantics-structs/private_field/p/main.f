struct (P) {
  private s := 2
  fn(get()) { return this.s }
  private fn(hid()) { return 5 }
  fn(callhid()) { return this.hid() }
}
p := P()
print(p.get())
print(p.callhid())
print(p.s)
