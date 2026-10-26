fn[async](f()) { return 1 }
p := f()
i := 0
seen := false
while(i < 5000) { if (p.done()) { seen = true }; i += 1 }
print(seen)
