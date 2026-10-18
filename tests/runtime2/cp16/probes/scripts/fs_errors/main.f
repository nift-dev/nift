// Recoverable (catchable) failures: code and message of every read/write path.
// Each line: <code> | <message>. Trace: failed reads emit nothing; write-family
// operations emit their line BEFORE the attempt with an empty result.
fn(show(e)) { print(e.code + " | " + e.message) }
try { open("missing.txt") } catch(e) { show(e) }
try { cat("missing.txt") } catch(e) { show(e) }
try { open_bytes("missing.txt") } catch(e) { show(e) }
try { sha256_file("missing.txt") } catch(e) { show(e) }
try { ifstream("missing.txt") } catch(e) { show(e) }
try { f := file("missing.txt"); f.open("r") } catch(e) { show(e) }
try { ls("missing-dir") } catch(e) { show(e) }
try { cd("missing-dir") } catch(e) { show(e) }
try { cp("missing.txt", "z.txt") } catch(e) { show(e) }
try { mv("missing.txt", "z.txt") } catch(e) { show(e) }
try { mkdir("notdir/sub") } catch(e) { show(e) }
try { touch("missing-dir/t.txt") } catch(e) { show(e) }
try { g := file("missing-dir/x.txt"); g.open("w"); g.write("q"); g.save() } catch(e) { show(e) }
try { ofstream("missing-dir/s.txt") } catch(e) { show(e) }
try { h := file("missing.txt"); h.remove(); print("file.remove missing: ok") } catch(e) { show(e) }
try { rm("missing.txt"); print("rm missing: ok") } catch(e) { show(e) }
try { cp("*.none", "dir") } catch(e) { show(e) }
print("end")
