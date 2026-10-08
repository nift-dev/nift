m := map()

i := 0
while(i < 8000) { k := (i % 37).to_string(); v := 0; if(m.contains(k)) { v = m.get(k) }; m.set(k,v+1); i += 1 }
print(m.size())

