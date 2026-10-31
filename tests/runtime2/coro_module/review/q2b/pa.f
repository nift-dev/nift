sq := async (x) => x * 2
fn(go(a)) { return await sq(a) }
export(go)
