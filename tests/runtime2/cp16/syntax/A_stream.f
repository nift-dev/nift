r := run("make", "-j4", {"mode": "stream"})
print("make exited " + r.exit_code.to_string())
