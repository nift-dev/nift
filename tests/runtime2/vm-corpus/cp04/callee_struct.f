struct(Acc) { total := 0; fn(add(n)) { total += n; return total } }
fn(run()) { a := Acc(0); a.add(2); return a.add(3) }
print(run())
