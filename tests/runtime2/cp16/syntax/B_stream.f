r := run("make", "-j4", {"stdout": "inherit", "stderr": "inherit"})
print(r.exit_code)
