fn[async](tick()) { i := 0; s := 0; while (i < 3) { sleep(1); j := 0; while (j < 2) { sleep(1); j += 1; if (j == 1) { continue }; s += j }; s += i; i += 1 }; return s }
xs := [1, 2, 3]
fn[async](each(v)) { t := 0; for (x : v) { sleep(1); if (x == 2) { continue }; t += x }; return t }
print(await tick())
print(await each(xs))
