import("fail.f")
ts := []
for (i := 0; i < 5; i += 1) { ts.push(thread(boom, i)) }
for (i := 0; i < 5; i += 1) { try { print(ts[i].join()) } catch(e) { print(e.message) } }
