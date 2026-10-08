counts := map()
i := 1
while(i <= 1000) {
 key := (i % 10).to_string()
 count := 0
 if(counts.contains(key)) { count = counts.get(key) }
 counts.set(key, count + 1)
 i += 1
}
print(counts.size())
print(counts.get("0"))
