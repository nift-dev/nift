fn(inc_thread(x, n)) { i := 0; while(i < n) { x++; i += 1 }; return true }
counter := atomic<int>(0)
a := thread(inc_thread, counter, 1000)
b := thread(inc_thread, counter, 1000)
a.join()
b.join()
print(counter.load())
