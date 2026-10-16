x := 3
print("v=$[x]")
fn(f()) { print("w=$[x]") }
f()
i := 0
while(i < 1) { print("loop=$[x]"); f(); i += 1 }
