i := 0
while(i < 3) {
  print(i)
  if(i == 1) { throw error("raised in loop") }
  i += 1
}
