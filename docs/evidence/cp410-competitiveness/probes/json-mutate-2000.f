a := inject("/home/nick/Repositories/nift/nift/.build/cp49/probes/records-2000.json")
s := 0

for(e : a) { e["total"] = e.v * 2; s += e["total"] }; print(s)

