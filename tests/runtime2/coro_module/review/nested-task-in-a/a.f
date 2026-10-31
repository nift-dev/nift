import("b.f")
fn(a_par()) { t := thread(b_get); return t.join() }
export(a_par)
