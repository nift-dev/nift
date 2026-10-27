fn[async](inner(n)) { sleep(n); e := getenv("NIFT_CP22_UNSET_INNER"); return n * 10 }
fn[async](outer(n)) { a := inner(n); b := inner(n + 1); x := getenv("NIFT_CP22_UNSET_OUTER"); return (await a) + (await b) }
r := outer(1)
print(await r)
q := outer(3)
print(await q)
m := getenv("NIFT_CP22_UNSET_MAIN")
print(type(m))
