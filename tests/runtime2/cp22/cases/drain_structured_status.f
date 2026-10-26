fn[async](inner()) { sleep(600); return 1 }
fn[async](outer()) { x := inner(); return 2 }
p := outer()
sleep(50)
print(p.status())
print(await p)
print(p.status())
