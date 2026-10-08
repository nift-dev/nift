a := inject("/home/nick/Repositories/nift/nift/.build/cp49/probes/records-2000.json")
s := 0

for(e : a) { s += e.v }; print(s)

