// B4: a null receiver without the method: legacy's malformed-expression error quoting the call.
print("before")
for (v : [" a ", null]) { print(v.to_upper()) }
