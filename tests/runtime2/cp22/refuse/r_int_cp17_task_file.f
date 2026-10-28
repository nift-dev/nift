fn[async](t()) { f := file("w.txt"); f.open("w"); f.write("x"); f.save(); f.close(); return 1 }
print(await t())
