fs := []
for (e : [1,2,3]) { fs.push(() => e); if (e == 2) { break } }
for (f : fs) { print(f()) }
