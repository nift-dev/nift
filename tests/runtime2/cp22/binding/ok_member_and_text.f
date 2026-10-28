o := {"sleep": (n) => { return n * 2 }}
for (sleep : [3]) { print("sleep(" + sleep.to_string() + ")") }
print(o.sleep(4))
