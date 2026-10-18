#!/usr/bin/env python3
"""CP14 built-ins: mutation check of the differential test (manual, CPU-heavy).

Each mutant changes one native in a copy of its source file, links the
differential test against it and must make `builtins-diff` (seed 1, 3,000
cases) report failures. Run from the repository root after
`make .build/rt/builtins-diff`:

    python3 tests/runtime2/builtins/builtins_mutants.py /path/to/legacy/nift [M1,M2,...]
"""
import os
import subprocess
import sys
import tempfile

NATIVES = "src/rt/natives"
CORE = ["value", "string", "aggregate", "atom", "heap_account", "compare", "number", "decimal", "render", "error",
        "handle", "legacy_bridge"]
FLAGS = ["-Isrc", "-Iinclude", "-Itests/rt", "-std=c++17", "-O2", "-pthread"]

MUTANTS = [
    ("M1 trim strips every isspace byte", "string_methods.cpp",
     'return trimmed(args, argc, out, error, "trim", trim_byte', 'return trimmed(args, argc, out, error, "trim", space_byte'),
    ("M2 index_of counts code points", "string_methods.cpp",
     "const std::size_t at = text.find(needle);  // a byte offset, as legacy",
     "std::size_t at = text.find(needle); if (at != std::string_view::npos) { std::size_t c = 0; "
     "utf8_count(text.substr(0, at), c); at = c; }"),
    ("M3 glob without the dot-file rule", "object_methods.cpp",
     "if (!name.empty() && name[0] == '.' && (pattern.empty() || pattern[0] != '.')) return false;", ""),
    ("M4 min/max accept mixed kinds", "array_methods.cpp",
     "        } else {\n            return fail(error, named(name, \": values must be comparable and homogeneous\"));",
     "        } else if (false) {\n            return fail(error, named(name, \": values must be comparable and homogeneous\"));"),
    ("M5 sum exact for large integers", "array_methods.cpp",
     "        total += number_approx(item);",
     "        total += item.is(Kind::Int) && item.as_int() > 9007199254740992 ? 1e300 : number_approx(item);"),
    ("M6 -0.0 is not a size", "support.cpp", "if (!std::isfinite(d) || d < 0.0", "if (!std::isfinite(d) || std::signbit(d)"),
    ("M7 split drops a trailing empty field", "string_methods.cpp",
     "if (at == std::string_view::npos) { if (!push(text.substr(pos))) return Status::Memory; break; }",
     "if (at == std::string_view::npos) { if (pos < text.size() && !push(text.substr(pos))) return Status::Memory; "
     "break; }"),
    ("M8 merge moves replaced keys to the end", "object_methods.cpp",
     "for (const auto& entry : object_entries(args[1])) object_set(result, entry.first, entry.second);",
     "for (const auto& entry : object_entries(args[1])) { object_remove(result, entry.first); "
     "object_set(result, entry.first, entry.second); }"),
    ("M9 to_int rejects a leading plus", "string_methods.cpp",
     "if (text.front() == '+') text.remove_prefix(1);", ""),
    ("M10 unique keeps duplicates", "array_methods.cpp",
     "        if (seen) continue;",
     "        if (seen) { result.push((*items)[i]); continue; }"),
    ("M11 JSON escapes in upper-case hex", "format_methods.cpp",
     'static constexpr char hex[] = "0123456789abcdef";', 'static constexpr char hex[] = "0123456789ABCDEF";'),
    ("M12 prettify indents by four", "format_methods.cpp",
     "if (pretty) out.append(static_cast<std::size_t>((depth + 1) * 2), ' ');",
     "if (pretty) out.append(static_cast<std::size_t>((depth + 1) * 4), ' ');"),
    ("M13 attr_escape keeps apostrophes", "format_methods.cpp",
     "else if (mode == Escape::Attr && c == '\\'') result += \"&#39;\";", ""),
    ("M14 large doubles in fixed notation", "support.cpp",
     "converted = std::to_chars(buffer, buffer + sizeof buffer, d, std::chars_format::general);",
     "converted = std::to_chars(buffer, buffer + sizeof buffer, d, std::chars_format::fixed);"),
    ("M15 is_number accepts numeric strings", "format_methods.cpp",
     'same(t, "int") || same(t, "float"))', 'same(t, "int") || same(t, "float") || same(t, "string"))'),
]


def compile_object(source, output, include=None):
    command = ["g++"] + FLAGS + ["-c", source, "-o", output]
    subprocess.run(command, check=True)


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        return 2
    legacy = sys.argv[1]
    only = sys.argv[2].split(",") if len(sys.argv) == 3 else None  # e.g. M7,M8
    mutants = [m for m in MUTANTS if not only or m[0].split()[0] in only]
    with tempfile.TemporaryDirectory(prefix="cp14-mutants-") as work:
        objects = []
        for name in CORE:
            out = os.path.join(work, name + ".o")
            compile_object("src/rt/%s.cpp" % name, out)
            objects.append(out)
        out = os.path.join(work, "RuntimeValue.o")
        compile_object("src/RuntimeValue.cpp", out)
        objects.append(out)
        natives = {}
        for name in sorted(os.listdir(NATIVES)):
            if name.endswith(".cpp"):
                out = os.path.join(work, "n_" + name[:-4] + ".o")
                compile_object(os.path.join(NATIVES, name), out)
                natives[name] = out
        detected = 0
        for title, file_name, before, after in mutants:
            with open(os.path.join(NATIVES, file_name)) as handle:
                text = handle.read()
            if before not in text:
                print("%s: pattern not found (stale mutant)" % title)
                return 1
            mutated = os.path.join(work, file_name)
            with open(mutated, "w") as handle:
                handle.write(text.replace(before, after, 1))
            mutated_object = os.path.join(work, "mutant.o")
            compile_object(mutated, mutated_object)
            binary = os.path.join(work, "diff")
            links = objects + [o for n, o in natives.items() if n != file_name] + [mutated_object]
            subprocess.run(["g++"] + FLAGS + ["tests/rt/builtins_diff.cpp"] + links + ["-o", binary], check=True)
            result = subprocess.run([binary, legacy, "1", "3000"], capture_output=True, text=True, errors="replace")
            summary = result.stdout.strip().splitlines()[-1]
            caught = result.returncode != 0
            detected += caught
            print("%-44s %s  %s" % (title, "DETECTED" if caught else "SURVIVED", summary))
        print("builtins_mutants: %d/%d detected" % (detected, len(mutants)))
        return 0 if detected == len(mutants) else 1


if __name__ == "__main__":
    sys.exit(main())
