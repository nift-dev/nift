// D-NEXT-refsem: collections with scalar contents and nested maps keep precise types.
adj := map()
adj.set(1, set())
adj.set(2, set())
s := adj.get(1)
s.add(2)
t := adj.get(1)
print(t.size())
freq := map()
for (w : ["a", "b", "a"]) {
    if (freq.contains(w)) { freq.set(w, freq.get(w) + 1) } else { freq.set(w, 1) }
}
print(freq.get("a"))
