#!/usr/bin/env python3
"""Regenerate src/migration_content.h from tests/fixtures/MIGRATION.md.

The embedded literal uses the same encoding style as src/handover_content.h:
\\n for newlines, \\" and \\\\ for quotes/backslashes, and octal escapes for any
non-ASCII byte (compiler source-charset independence). Byte-exact.

Usage: python3 scripts/gen_migration_content.py
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "MIGRATION.md"
HEADER = ROOT / "src" / "migration_content.h"

data = FIXTURE.read_bytes()
out = bytearray()
for b in data:
    if b == 0x0A:
        out += b"\\n"
    elif b in (ord('\\'), ord('"')):
        out += b"\\" + bytes([b])
    elif 0x20 <= b <= 0x7E:
        out.append(b)
    else:
        out += f"\\{b:03o}".encode("ascii")

header = (
    "// Embedded canonical init --migration content.\n"
    "// Update tests/fixtures/MIGRATION.md and regenerate this byte-exact literal together.\n"
    "// Non-ASCII bytes are octal escaped for compiler source-charset independence.\n"
    "//\n"
    "// Regenerate with: python3 scripts/gen_migration_content.py\n"
    "\n"
    "#pragma once\n"
    "\n"
    "constexpr const char* migration_content =\n"
    '    "' + out.decode("ascii") + '";\n'
)
HEADER.write_text(header, encoding="ascii")
print(f"{HEADER.relative_to(ROOT)} regenerated ({len(out)} bytes fixture)")


def verify(symbol: str, fixture: Path, header: Path) -> None:
    text = header.read_text()
    m = __import__("re").search(r'constexpr const char\* ' + symbol + r' =\n    "(.*)";', text, flags=__import__("re").S)
    assert m, f"could not extract {symbol} literal"
    lit = m.group(1)
    decoded = bytearray()
    i = 0
    while i < len(lit):
        c = lit[i]
        if c == "\\" and i + 1 < len(lit):
            nxt = lit[i + 1]
            if nxt in "\\\"":
                decoded.append(ord(nxt)); i += 2; continue
            if nxt == "n":
                decoded.append(0x0A); i += 2; continue
            if nxt in "01234567":
                j = i + 1
                while j < min(i + 4, len(lit)) and lit[j] in "01234567":
                    j += 1
                decoded.append(int(lit[i + 1:j], 8)); i = j; continue
            raise SystemExit(f"unhandled escape at {i}")
        decoded.append(ord(c)); i += 1
    assert bytes(decoded) == fixture.read_bytes(), f"{header.name} does not decode to {fixture.name}"


verify("migration_content", FIXTURE, HEADER)
print("verify: src/migration_content.h decodes byte-exact to tests/fixtures/MIGRATION.md")