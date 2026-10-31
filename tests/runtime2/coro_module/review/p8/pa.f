fn(w(x)) { return x * 2 }
o := {"t": thread}
v := o.t(w, 3).join()
fn(get()) { return v }
export(get)
