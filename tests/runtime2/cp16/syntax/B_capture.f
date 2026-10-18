r := run("git", "rev-parse", "HEAD", {"stdout": "capture", "stderr": "capture"})
if(r.launched && r.exit_code == 0) { print(r.stdout.trim()) } else { print("git failed") }
