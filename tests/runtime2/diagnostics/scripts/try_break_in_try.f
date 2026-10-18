i := 0
while(i < 5) {
  try {
    if(i == 2) { break }
  } catch(e) {
    print("caught")
  }
  print(i)
  i += 1
}
print("end")
