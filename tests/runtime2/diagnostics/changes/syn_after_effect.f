f := file("made-before-error.txt")
f.open("w")
f.write("written")
f.save()
f.close()
print("wrote")
y := (2 *
