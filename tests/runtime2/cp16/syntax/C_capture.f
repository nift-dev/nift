r := cmd("git", "rev-parse", "HEAD").run()
if(r.launched && r.exit_code == 0) { print(r.stdout.trim()) } else { print("git failed") }
