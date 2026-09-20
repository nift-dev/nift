#!/usr/bin/env python3
"""Guard against case-insensitive header-name collisions with common MinGW/
MSYS2 system headers.

On Windows/MSYS2 (case-insensitive filesystem), a Nift source header whose
name differs only by case from a system header can be resolved instead of the
system one when `-Isrc` is searched. The real case that broke the Windows
build: Nift's `Process.h` vs MinGW's `<process.h>` (included transitively by
`pthread.h`), which re-entered the Nift header mid-`<filesystem>` and broke
the gthread/locale chain.

Scan the project headers and fail if any basename (case-insensitive) matches a
known system header name.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Common MinGW/MSYS2 system header basenames (lowercase) that Nift headers
# must never shadow. Extend as new system collisions are discovered.
SYSTEM_HEADERS = {
    "process.h", "io.h", "math.h", "signal.h", "stdio.h", "stdlib.h",
    "string.h", "time.h", "fcntl.h", "unistd.h", "direct.h", "conio.h",
    "errno.h", "locale.h", "limits.h", "float.h", "ctype.h", "wchar.h",
    "assert.h", "setjmp.h", "stdarg.h", "stdint.h", "pthread.h", "sched.h",
}

def main():
    bad = []
    for p in sorted((ROOT / "src").glob("*.h")) + sorted((ROOT / "src").glob("*.hpp")):
        lower = p.name.lower()
        if lower in SYSTEM_HEADERS:
            bad.append(f"{p.name} (lowercased '{lower}') shadows a MinGW system header")
    if bad:
        print("header-collision FAIL:", file=sys.stderr)
        for b in bad:
            print("  -", b, file=sys.stderr)
        return 1
    print("header-name collisions: none")
    return 0


if __name__ == "__main__":
    sys.exit(main())
