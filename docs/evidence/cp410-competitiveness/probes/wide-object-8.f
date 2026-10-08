obj := {"k0":0,"k1":1,"k2":2,"k3":3,"k4":4,"k5":5,"k6":6,"k7":7}
total := 0
i := 0
while(i < 2000) { total += obj["k7"]; i += 1 }
print(total)
