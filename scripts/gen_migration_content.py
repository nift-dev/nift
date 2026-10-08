#!/usr/bin/env python3
"""Generate/check canonical migration, rewrite and redesign workbook literals.

Core fixtures own the bytes. Non-ASCII bytes use octal escapes for compiler
source-charset independence. --check verifies drift without rewriting files.
"""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parent.parent

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



def generate(name: str) -> tuple[Path, Path, str]:
    fixture = ROOT / "tests" / "fixtures" / (name.upper() + ".md")
    header = ROOT / "src" / (name + "_content.h")
    out = bytearray()
    for b in fixture.read_bytes():
        if b == 10:
            out += b"\\n"
        elif b in (92, 34):
            out += bytes([92, b])
        elif 32 <= b <= 126:
            out.append(b)
        else:
            out += f"\\{b:03o}".encode("ascii")
    content = (f"// Embedded canonical init --{name} content.\n"
               f"// Update tests/fixtures/{name.upper()}.md and regenerate this byte-exact literal together.\n"
               "// Non-ASCII bytes are octal escaped for compiler source-charset independence.\n"
               "//\n// Regenerate with: python3 scripts/gen_migration_content.py\n\n#pragma once\n\n"
               f"constexpr const char* {name}_content =\n    \"" + out.decode("ascii") + "\";\n")
    return fixture, header, content


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for name in ("migration", "rewrite", "redesign"):
        fixture, header, content = generate(name)
        if args.check:
            if not header.exists() or header.read_text(encoding="ascii") != content:
                raise SystemExit(f"canonical drift: {header.relative_to(ROOT)}")
        elif not header.exists() or header.read_text(encoding="ascii") != content:
            header.write_text(content, encoding="ascii")
        verify(name + "_content", fixture, header)
        print(f"verify: {header.relative_to(ROOT)} byte-exact ({len(fixture.read_bytes())} bytes)")


if __name__ == "__main__":
    main()
