fn(w(x)) { return x * 2 }
	v := thread(w, 3).join()
fn(get()) { return v }
export(get)
