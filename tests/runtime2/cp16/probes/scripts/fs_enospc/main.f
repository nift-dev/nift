// ENOSPC via /dev/full. NOTE: never file("/dev/full").open("w"/"a"/"rw"):
// legacy reads the existing content first, unbounded (OOM; quirk H-Q4).
fn(show(e)) { print(e.code + " | " + e.message) }
o := ofstream("/dev/full")
try { o.write("x"); o.flush() } catch(e) { show(e) }
try { close(o) } catch(e) { show(e) }
p := ofstream("/dev/full")
try { p.write_line("y") } catch(e) { show(e) }
try { close(p) } catch(e) { show(e) }
r := cmd("sh", "-c", "echo data").stdout("/dev/full").run()
print(r.stringify())
print(run("sh", "-c", "echo data > /dev/full").stringify())
print("end")
