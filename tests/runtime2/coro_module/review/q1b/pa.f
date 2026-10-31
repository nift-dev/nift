fn(go(a)) { o := {"f": async (x) => x * 2}; return await o.f(a) }
export(go)
