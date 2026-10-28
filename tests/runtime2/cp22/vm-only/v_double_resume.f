fn[async](t()) { sleep(10); print("b"); sleep(60); print("c"); return 1 }
p := t()
sleep(35)
print("m")
await p
