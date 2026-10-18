print("Preparing authored MDX before Nift page rendering...")
result := run("node", "tools/pre-build.mjs")
if(result.stdout != "") { print(result.stdout) }
if(result.exit_code != 0) { throw error("MDX preparation failed; see preparation diagnostics. " + result.stderr, "user.mdx_prebuild") }
print("MDX preparation complete; starting Nift page rendering.")
