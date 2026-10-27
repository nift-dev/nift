fn[async](tick()) { sleep(5); return 1 }
t := tick()
print(is_dir("."))
print(await t)
