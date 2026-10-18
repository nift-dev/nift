// option errors are argument errors (fatal, as `run: arguments must be scalar` today);
// launch failure and deadlines are result values, never exceptions.
r := run("definitely-missing", {"mode": "capture"})
print(r.launched)
print(r.exit_code)
print(r.error)
