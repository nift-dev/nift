if (getenv("NIFT_PIPELINE_PREPARED") != "1") {
    tracked := file(".nift/tracked.json")
    tracked.open("r")
    previous := tracked.read()
    tracked.close()
    bun_path := which("bun")
    if (bun_path == null) { bun_path = getenv("HOME") + "/.bun/bin/bun" }
    preparation := cmd(bun_path, "tools/prepare.ts").cwd(".").run()
    print(preparation.stdout)
    if (preparation.exit_code != 0) { error("Publication preparation failed") }
    tracked.open("r")
    current := tracked.read()
    tracked.close()
    if (previous != current) { error("Route inventory changed; run bun run build to load the updated inventory") }
}
