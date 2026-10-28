s := "0123456789abcdef"
k := 0
while (k < 19) { s = s + s; k += 1 }
fn[async](work(n)) { i := 0; t := 0; while (i < n) { sleep(1); u := s + "x"; t += u.length(); i += 1 }; return t }
a := work(400)
b := work(400)
print((await a) + (await b))
