p := run("sh", "-c", "yes | gzip > /dev/null", {"wait": false})
p.cancel({"kill_after_ms": 500})
r := p.wait()
print(r.cancelled)
