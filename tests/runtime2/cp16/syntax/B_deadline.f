r := run("curl", "-s", url, {"timeout_ms": 5000})
print(r.timed_out)
