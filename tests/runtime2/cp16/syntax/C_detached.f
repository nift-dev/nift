srv := cmd("python3", "-m", "http.server", "8000").detach()
print(srv.pid)
