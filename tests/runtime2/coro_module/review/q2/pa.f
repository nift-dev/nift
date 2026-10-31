sq := async (x) => x * x
fn(go(a)) { return await sq(a) }
export(go)
