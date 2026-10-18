r := run("git", "rev-parse", "HEAD")
if(!r.launched) { print("cannot start git: " + r.error) } else { if(r.exit_code != 0) { print("git failed: " + r.stderr) } else { print(r.stdout.trim()) } }
