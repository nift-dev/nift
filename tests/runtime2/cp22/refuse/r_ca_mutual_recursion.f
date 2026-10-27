fn(even(n)) { if (n == 0) { return true }; return odd(n - 1) }
fn(odd(n)) { if (n == 0) { return false }; return even(n - 1) }
fn[async](t(n)) { return even(n) }
print(await t(10))
