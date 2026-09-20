#include "CLI.h"
#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
    // A native Windows binary gets text-mode stdio by default, which turns
    // every '\n' written to a pipe/console into '\r\n'. That breaks exact
    // stdout comparisons in scripted/tooling contexts (and Nift's own tests)
    // which expect '\n' like every other platform. Use binary mode so output
    // is byte-identical to POSIX.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    return run_cli(argc, argv);
}