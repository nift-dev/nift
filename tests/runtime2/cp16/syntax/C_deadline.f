r := cmd("curl", "-s", url).timeout(5000).run()
print(r.timed_out)
