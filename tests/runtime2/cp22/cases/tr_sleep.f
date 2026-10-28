fn[async](f(n)) { sleep(n); e := getenv("NIFT_CP22_UNSET"); return n }
a := f(5)
sleep(1)
print(await a)
