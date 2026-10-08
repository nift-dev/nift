arr := inject("/home/nick/Repositories/nift/scripting-benchmark/benchmarks/fixtures/structured-data/records-small.json")
total := 0
for(e : arr) { total += e.v }
print(total)
