#pragma once
// `nift serve`: a local development convenience wrapper equivalent to
//   python3 -m http.server PORT --bind HOST --directory DIR
// argv[1] is "serve"; options start at argv[2]. Returns the process exit
// status (on POSIX a successful launch never returns: Python replaces Nift).
int run_serve(int argc, char** argv);
