fn(sum(n)) { t := 0; i := 0; while(i < n) { t += i; i += 1 } return t }
print(sum(10))
fn(each(xs)) { out := []; for(x : xs) { out.push(x * 2) } return out }
print(each([1, 2, 3]))
