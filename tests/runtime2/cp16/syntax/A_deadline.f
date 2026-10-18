r := run("curl", "-s", url, {"timeout_ms": 5000, "kill_after_ms": 1000})
if(r.timed_out) { print("too slow") } else { print(r.stdout.length()) }
