fn(boom(x)) { if (x > 2) { throw error("big " + x, "user.big") }; return x }
export(boom)
