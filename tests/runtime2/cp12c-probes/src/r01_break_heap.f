o := {"a":[1,2],"b":[3]}
for ((k,v) : o) { if (k == "a") { break } }
print("done")
