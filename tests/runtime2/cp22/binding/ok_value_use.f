for (range : ["1.x", "2.x"]) { print("range " + range) }
for ((key, label) : {"a": "A"}) { print(key + "=" + label) }
try { throw error("boom") } catch (err) { print(err.message) }
