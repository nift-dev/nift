result := cmd("node", "scripts/build-worker.mjs").cwd(getenv("NIFT_HOOK_ROOT")).run()
if(!result.launched || result.exit_code != 0) { throw error("Worker packaging failed: " + result.stderr, "user.build.tool_failed") }
