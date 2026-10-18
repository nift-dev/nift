srv := run("python3", "-m", "http.server", "8000", {"mode": "detached"})
print("server pid " + srv.pid.to_string())
