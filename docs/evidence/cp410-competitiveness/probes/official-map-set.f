s := set()
i := 1
while(i <= 1000) { s.add(i); i += 1 }
print(s.size())
if(s.contains(500)) { print(1) } else { print(0) }
if(s.contains(1001)) { print(1) } else { print(0) }
