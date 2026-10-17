// With or without NIFT_TRACE_HOST the script and its children see the same
// environment and descriptors (trace_format.md: descriptor take-over).
print(getenv("NIFT_TRACE_HOST"))
r := cmd("sh", "-c", "echo ${NIFT_TRACE_HOST:-unset} > env.txt; ls /proc/self/fd | wc -l > fds.txt").run()
print(cat("env.txt"))
