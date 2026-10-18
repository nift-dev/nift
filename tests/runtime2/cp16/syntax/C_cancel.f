p := cmd("yes").pipe(cmd("gzip")).kill_after(500).start()
p.cancel()
print(p.wait().cancelled)
