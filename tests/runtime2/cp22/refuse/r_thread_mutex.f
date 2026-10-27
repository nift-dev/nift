fn(inc(m, n)) {
    i := 0
    while(i < n) {
        m.lock()
        v := m.get()
        m.set(v + 1)
        m.unlock()
        i += 1
    }
    return true
}
m := mutex(0)
a := thread(inc, m, 250)
b := thread(inc, m, 250)
c := thread(inc, m, 250)
a.join(); b.join(); c.join()
m.lock()
print(m.get())
m.unlock()
