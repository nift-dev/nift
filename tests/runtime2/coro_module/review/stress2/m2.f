fn(m2_boom(x)) { sleep(x % 3); throw error("deep " + x, "user.deep") }
export(m2_boom)
