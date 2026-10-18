// EACCES (run without CAP_DAC_OVERRIDE so it is real even as root).
fn(show(e)) { print(e.code + " | " + e.message) }
try { open("noread.txt") } catch(e) { show(e) }
try { cat("noread.txt") } catch(e) { show(e) }
try { open_bytes("noread.txt") } catch(e) { show(e) }
try { sha256_file("noread.txt") } catch(e) { show(e) }
try { ifstream("noread.txt") } catch(e) { show(e) }
try { touch("ro/new.txt") } catch(e) { show(e) }
try { mkdir("ro/sub") } catch(e) { show(e) }
try { f := file("ro/in.txt"); f.open("rw"); f.write("w"); f.save() } catch(e) { show(e) }
try { cp("noread.txt", "copy.txt") } catch(e) { show(e) }
try { cp("ro/in.txt", "ro/copy.txt") } catch(e) { show(e) }
try { mv("ro/in.txt", "moved.txt") } catch(e) { show(e) }
try { rm("ro/in.txt") } catch(e) { show(e) }
try { ls("nox") } catch(e) { show(e) }
try { print(exists("nox/hidden.txt")) } catch(e) { show(e) }
try { print(stat("nox/hidden.txt").stringify()) } catch(e) { show(e) }
try { cd("nox") } catch(e) { show(e) }
print(run("./noread.txt").stringify())
print("end")
