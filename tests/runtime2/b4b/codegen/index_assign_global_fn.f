// P1: a global array assigned from a function, the value used as an argument.
xs := [1]
fn(f()) { return [xs[0] = 9] }
print(f())
