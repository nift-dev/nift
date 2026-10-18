result := run("node", "tools/prepare.mjs")
if(result.exit_code != 0) { throw error(result.stderr, "user.capgo_prepare") }
print(result.stdout)
