// B4: a union with a mutex is not a plain family: refused (unchanged).
print("before")
for (v : [" a ", mutex()]) { print(v.trim()) }
