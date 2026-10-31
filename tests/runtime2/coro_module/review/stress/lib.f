import("deep.f")
fn(work(i)) { acc := 0; lj := 0; while (lj < 200) { acc += deep_mix(i, lj); lj += 1 }; if (i % 7 == 3) { throw error("bad " + i, "user.bad") }; return acc }
fn[async](awork(i)) { return work(i) + 1 }
fn(sum_par(n)) { lts := []; lk := 0; while (lk < n) { lts.push(thread(deep_mix, lk, 2)); lk += 1 }; ltot := 0; lk = 0; while (lk < n) { ltot += lts[lk].join(); lk += 1 }; return ltot }
export(work)
export(awork)
export(sum_par)
