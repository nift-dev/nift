@import("semver")

fn(expect(condition, label)) {
    if(!condition) { print("FAIL " + label); missing.value }
}

p := semver.parse("1.2.3-alpha.1+build.007")
expect(p.valid && p.error == null && p.error_code == null, "parse success status")
expect(p.major == 1 && p.minor == 2 && p.patch == 3, "parse core")
expect(p.prerelease.stringify() == "[\"alpha\",\"1\"]" && p.build.stringify() == "[\"build\",\"007\"]", "parse identifiers")
expect(p.raw == "1.2.3-alpha.1+build.007" && p.keys().length() == 9, "parse stable shape")
bad := semver.parse("01.2.3")
expect(!bad.valid && bad.error != null && bad.error_code == "leading_zero" && bad.major == null && bad.prerelease.length() == 0 && bad.raw == "01.2.3", "parse failure shape")
typed := semver.parse(123)
expect(!typed.valid && typed.error_code == "invalid_type" && typed.raw == null, "parse invalid type")
expect(!semver.valid(null), "valid non-string")
expect(semver.compare(null, "1.0.0") == null && semver.compare("1.0.0", []) == null, "compare non-string")
expect(!semver.eq({}, "1.0.0"), "eq non-string")
expect(!semver.lt(true, "1.0.0"), "lt non-string")
expect(!semver.lte(1, "1.0.0"), "lte non-string")
expect(!semver.gt("1.0.0", null), "gt non-string")
expect(!semver.gte("1.0.0", []), "gte non-string")
expect(!semver.valid_range({}), "valid_range non-string")
expect(!semver.satisfies(null, "*") && !semver.satisfies("1.0.0", false), "satisfies non-string")

valid_versions := [
    "0.0.0", "1.2.3", "2147483647.2147483647.2147483647",
    "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-0.3.7", "1.0.0-x.7.z.92",
    "1.0.0-x-y-z.--", "1.0.0+001", "1.0.0-beta+exp.sha.5114f85"
]
for(version : valid_versions) { expect(semver.valid(version), "valid " + version) }

invalid_versions := [
    "", "1", "1.2", "1.2.3.4", "v1.2.3", " 1.2.3", "1.2.3 ",
    "01.2.3", "1.02.3", "1.2.03", "2147483648.0.0", "0.2147483648.0", "0.0.2147483648",
    "1.2.3-", "1.2.3+", "1.2.3-alpha..1", "1.2.3+build..1", "1.2.3-01",
    "1.2.3-alpha_1", "1.2.3+build_1", "1.2.3-alpha+one+two", "1.2.3-alpha beta"
]
non_ascii_letter := bytes([195, 169]).decode("utf-8")
non_ascii_digit := bytes([217, 161]).decode("utf-8")
invalid_versions.push("1.2.3-" + non_ascii_letter)
invalid_versions.push("1.2.3-" + non_ascii_digit)
invalid_versions.push("1.2.3+" + non_ascii_letter)
for(version : invalid_versions) { expect(!semver.valid(version), "invalid " + version) }

chain := [
    "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
    "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"
]
i := 0
while(i < chain.length() - 1) {
    expect(semver.compare(chain[i], chain[i + 1]) == -1, "official precedence forward")
    expect(semver.compare(chain[i + 1], chain[i]) == 1, "official precedence reverse")
    i += 1
}
expect(semver.eq("1.2.3+one", "1.2.3+two"), "build equality")
expect(semver.compare("1.0.0-2", "1.0.0-11") == -1, "numeric prerelease")
expect(semver.compare("1.0.0-11", "1.0.0-a") == -1, "numeric before lexical")
expect(semver.compare("1.0.0-a", "1.0.0-b") == -1, "lexical prerelease")
expect(semver.lt("1.0.0", "2.0.0") && semver.lte("1.0.0", "1.0.0"), "lower predicates")
expect(semver.gt("2.0.0", "1.0.0") && semver.gte("1.0.0", "1.0.0"), "upper predicates")
expect(semver.compare("bad", "1.0.0") == null, "invalid compare")
expect(!semver.eq("bad", "bad") && !semver.lt("bad", "1.0.0") && !semver.lte("bad", "1.0.0") && !semver.gt("bad", "1.0.0") && !semver.gte("bad", "1.0.0"), "invalid predicates")

valid_ranges := [
    "1.2.3", "=1.2.3", ">1.2.3", ">=1.2.3", "<2.0.0", "<=2.0.0",
    "*", "x", "X", "1.x", "1.X", "1.*", "1.2.x", "1.2.X", "1.2.*",
    "~1.2", "~1.2.3", "^1.2.3", "^0.2.3", "^0.0.3",
    "1.2.3 - 2.3.4", ">=1.2.3 <2.0.0", ">=1.2.3\t<2.0.0", ">2.0.0 <1.0.0",
    "=1.2.3-alpha+build", ">=1.2.3-alpha <1.2.3"
]
vertical_tab := bytes([11]).decode("utf-8")
form_feed := bytes([12]).decode("utf-8")
valid_ranges.push(">=1.2.3" + vertical_tab + "<2.0.0")
valid_ranges.push(">=1.2.3" + form_feed + "<2.0.0")
for(range : valid_ranges) { expect(semver.valid_range(range), "valid range " + range) }

invalid_ranges := [
    "", " ", "1", "1.2", "v1.2.3", "latest", "1.2.3 || 2.0.0", "1.2.3, <2.0.0",
    "(1.2.3)", ">= 1.2.3", "1.2.3 - 2.3.4 >=1.0.0", "1.2 - 2.3.4",
    "1.2.3 - 2.3", "~1", "~1.2.x", "^1.2", "1.2.3-", "1.2.3 + 2.0.0"
]
for(range : invalid_ranges) { expect(!semver.valid_range(range), "invalid range " + range) }

expect(semver.satisfies("1.2.3", "1.2.3") && !semver.satisfies("1.2.4", "1.2.3"), "exact")
expect(semver.satisfies("1.9.9", "1.x") && !semver.satisfies("2.0.0", "1.x"), "major wildcard")
expect(semver.satisfies("1.2.99", "1.2.x") && !semver.satisfies("1.3.0", "1.2.x"), "minor wildcard")
expect(semver.satisfies("2147483647.9.9", "2147483647.x"), "maximum major wildcard")
expect(semver.satisfies("1.2.9", "~1.2") && !semver.satisfies("1.3.0", "~1.2"), "tilde partial")
expect(semver.satisfies("1.2.9", "~1.2.3") && !semver.satisfies("1.3.0", "~1.2.3"), "tilde full")
expect(semver.satisfies("1.9.0", "^1.2.3") && !semver.satisfies("2.0.0", "^1.2.3"), "caret major")
expect(semver.satisfies("0.2.9", "^0.2.3") && !semver.satisfies("0.3.0", "^0.2.3"), "caret zero minor")
expect(semver.satisfies("0.0.3+build", "^0.0.3") && !semver.satisfies("0.0.4", "^0.0.3"), "caret zero patch")
expect(semver.satisfies("2.0.0", "1.2.3 - 2.3.4") && semver.satisfies("2.3.4+meta", "1.2.3 - 2.3.4") && !semver.satisfies("2.3.5", "1.2.3 - 2.3.4"), "hyphen")
expect(semver.satisfies("1.5.0", ">=1.2.3 <2.0.0") && !semver.satisfies("2.0.0", ">=1.2.3 <2.0.0"), "intersection")
expect(semver.satisfies("1.5.0", ">=1.2.3\t<2.0.0"), "ASCII whitespace intersection")
expect(semver.satisfies("1.5.0", ">=1.2.3" + vertical_tab + "<2.0.0"), "VT intersection")
expect(semver.satisfies("1.5.0", ">=1.2.3" + form_feed + "<2.0.0"), "FF intersection")
expect(semver.valid_range(">2.0.0 <1.0.0") && !semver.satisfies("1.5.0", ">2.0.0 <1.0.0"), "contradictory valid grammar")

expect(!semver.satisfies("1.5.0-alpha", ">=1.0.0 <2.0.0"), "stable range excludes prerelease")
expect(semver.satisfies("1.2.3-alpha", "=1.2.3-alpha"), "exact prerelease")
expect(semver.satisfies("1.2.3-beta", ">=1.2.3-alpha <1.2.3"), "same tuple prerelease gate")
expect(!semver.satisfies("1.2.4-alpha", ">=1.2.3-alpha <2.0.0"), "different tuple prerelease gate")
expect(!semver.satisfies("1.2.3-alpha", "*"), "wildcard excludes prerelease")
expect(semver.satisfies("1.2.3-alpha+two", "1.2.3-alpha+one"), "exact ignores build")

long_identifier := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
expect(semver.valid("1.2.3-" + long_identifier), "identifier byte boundary")
expect(!semver.valid("1.2.3-" + long_identifier + "a"), "identifier byte overflow")
identifiers32 := "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a"
expect(semver.valid("1.2.3-" + identifiers32), "identifier count boundary")
expect(!semver.valid("1.2.3-" + identifiers32 + ".a"), "identifier count overflow")
expect(semver.valid("1.2.3+" + "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa." + "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb." + "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"), "admitted long version")
version256 := "1.2.3+" + "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa." + "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb." + "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc." + "ddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
expect(version256.encode("utf-8").length() == 256 && semver.valid(version256), "version byte boundary")
expect(!semver.valid("1.2.3+" + "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa." + "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb." + "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc." + "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"), "version byte overflow")

structural_tokens32 := "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *"
expect(semver.valid_range(structural_tokens32), "structural token count boundary")
expect(!semver.valid_range(structural_tokens32 + " *"), "structural token count overflow")
expect(!semver.valid_range("1.2.3 - 2.3.4 - 3.4.5"), "malformed multi hyphen")
range1024 := "1.2.3"
range_bytes := range1024.encode("utf-8").length()
while(range_bytes < 1024) { range1024 += " "; range_bytes += 1 }
expect(range1024.encode("utf-8").length() == 1024 && semver.valid_range(range1024), "range byte boundary")
expect(!semver.valid_range(range1024 + " "), "range byte overflow")

overlong_error := semver.parse(version256 + "x")
expect(overlong_error.error_code == "version_too_long", "length error precedence")
identifier_error := semver.parse("1.2.3-" + long_identifier + "_")
expect(identifier_error.error_code == "identifier_too_long", "identifier length error precedence")
count_error := semver.parse("1.2.3-" + identifiers32 + ".a")
expect(count_error.error_code == "too_many_identifiers", "identifier count error precedence")
overflow_error := semver.parse("2147483648.0.0")
expect(overflow_error.error_code == "numeric_overflow", "numeric overflow error precedence")

first := semver.parse("1.2.3-alpha.1+build").stringify()
expect(first == semver.parse("1.2.3-alpha.1+build").stringify(), "deterministic parse")
invalid_first := semver.parse("01.2.3").stringify()
expect(invalid_first == semver.parse("01.2.3").stringify(), "deterministic failure")
expect(semver.satisfies("1.5.0", ">=1.2.3 <2.0.0") == semver.satisfies("1.5.0", ">=1.2.3 <2.0.0"), "deterministic range")
print("PASS semver contract")
