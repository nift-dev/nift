result := cmd("node", "scripts/build-frontend.mjs").cwd(getenv("NIFT_HOOK_ROOT")).run()
if(!result.launched || result.exit_code != 0) { throw error("Frontend compilation failed: " + result.stderr, "user.build.tool_failed") }
