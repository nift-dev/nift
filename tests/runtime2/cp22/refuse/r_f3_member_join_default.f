say := (s) => { print(s); return 1 }
o := {"join": say}
fn[async](t(io)) { sleep(5); return io.join("x") }
print(await t(o))
