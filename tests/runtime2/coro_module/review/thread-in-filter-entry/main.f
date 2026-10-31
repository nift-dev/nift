print("pre")
import("counter.f")
fn(w(x)) { return peek() }
rd := w
k := (x) => thread(rd, x).join()
print([1].map(k)[0])
