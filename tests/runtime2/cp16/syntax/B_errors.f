r := run("definitely-missing", {"stdout": "capture"})
print(r.launched)
print(r.error)
