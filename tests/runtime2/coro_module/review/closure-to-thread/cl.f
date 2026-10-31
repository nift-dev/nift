secret := 9
fn(mk()) { return () => secret }
export(mk)
