result := cmd("node", "scripts/probe-full-api-build.mjs").cwd(getenv("NIFT_HOOK_ROOT")).run()
if(!result.launched || result.exit_code != 0) { throw error("Backend compilation failed: " + result.stderr, "user.build.tool_failed") }
output := file(getenv("NIFT_HOOK_OUTPUT"))
output.open("w")
output.write("{\"compiled\":true}")
output.save()
output.close()
