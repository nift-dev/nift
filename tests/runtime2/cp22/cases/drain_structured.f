fn[async](inner()) { sleep(50); print("inner done"); return 1 }
fn[async](outer()) { x := inner(); return 2 }
p := outer()
print(await p)
print("after")
