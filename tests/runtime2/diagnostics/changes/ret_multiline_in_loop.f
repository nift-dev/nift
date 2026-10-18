fn(pair(i)) {
  return [i,
    i * 2]
}
i := 0
while(i < 2) {
  print(pair(i).stringify())
  i += 1
}
