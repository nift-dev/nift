arr := []
i := 1
while(i <= 1000) { arr.push({"k": i, "v": i % 7}); i += 1 }
total := 0
for(e : arr) { e["total"] = e.v * 2; total += e["total"] }
print(total)
