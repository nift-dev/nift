// B4 (b4-dispatch.md 6, owner): presenting an atomic while another thread updates it is
// one sequentially consistent load per presentation (shared_atomic_load, as print), with
// no data race: the TSan lane (test-runtime2-b4-tsan, combined certification) runs this.
// The interpolated values race by design; only the final state is printed.
fn(work(c)) {
  i := 0
  while (i < 500) { c.fetch_add(1); i += 1 }
  return 0
}
a := atomic<int>(0)
t := thread(work, a)
n := 0
while (n < 500) {
  s := "v=$[a]"
  n += 1
}
t.join()
print("final $[a]")
print(n)
