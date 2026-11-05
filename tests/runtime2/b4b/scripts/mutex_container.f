// B4b C7: a container held by a mutex and changed by a thread: the value read back
// (m.get()) is of unknown origin to the inference (anything), never a stale element type.
fn(job(m)) {
  m.lock()
  s := m.get()
  s["v"] = 5
  m.set(s)
  m.unlock()
  return 1
}
m := mutex({"v": " a "})
t := thread(job, m)
t.join()
m.lock()
s := m.get()
m.unlock()
print("before")
print(s.v.trim())
