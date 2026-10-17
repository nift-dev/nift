calls := 0
fn(side()) { calls += 1; return true }
if(false && side()) { print("no") }
if(true || side()) { print("yes") }
print(calls)
