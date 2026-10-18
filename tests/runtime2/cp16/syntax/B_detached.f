srv := run("python3", "-m", "http.server", "8000", {"detach": true})
print(srv.pid)
