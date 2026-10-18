@import("diff")

count := 0
fn(expect(condition, label)) {
    if(!condition) { print("FAIL " + label); missing.value }
    count += 1
}

fn(sequence_shape(result, label)) {
    expect(result.keys().size() == 4 && result.keys().contains("ok") && result.keys().contains("changes") && result.keys().contains("error") && result.keys().contains("error_code"), label + " result shape")
    if(result.ok) {
        expect(result.error == null && result.error_code == null, label + " success errors")
        previous := ""
        for(change : result.changes) {
            expect(change.keys().size() == 2 && change.keys().contains("kind") && change.keys().contains("values"), label + " change shape")
            expect(["equal", "delete", "insert"].contains(change.kind), label + " kind")
            expect(change.values.size() > 0 && change.kind != previous, label + " coalescing")
            previous = change.kind
        }
    } else { expect(result.changes.size() == 0 && result.error != null && result.error_code != null, label + " failure shape") }
}

fn(reconstruct(result, before, after, label)) {
    old := ""
    new := ""
    for(change : result.changes) {
        if(change.kind != "insert") { old += change.values.join("") }
        if(change.kind != "delete") { new += change.values.join("") }
    }
    expect(old == before && new == after, label + " reconstruction")
}

empty := diff.lines("", "")
sequence_shape(empty, "empty")
expect(empty.ok && empty.changes.size() == 0, "empty changes")
same := diff.chars("abc", "abc")
expect(same.changes.stringify() == '[{"kind":"equal","values":["a","b","c"]}]', "identical chars")
replace := diff.chars("abc", "adc")
expect(replace.changes.stringify() == '[{"kind":"equal","values":["a"]},{"kind":"delete","values":["b"]},{"kind":"insert","values":["d"]},{"kind":"equal","values":["c"]}]', "char replacement and tie")
sequence_shape(replace, "replace")
reconstruct(replace, "abc", "adc", "replace")
expect(diff.chars("A", "B").changes.stringify() == '[{"kind":"delete","values":["A"]},{"kind":"insert","values":["B"]}]', "delete tie first")
expect(diff.chars("ABA", "BA").changes.stringify() == '[{"kind":"delete","values":["A"]},{"kind":"equal","values":["B","A"]}]', "repeated token tie")
expect(diff.chars("", "abc").changes.stringify() == '[{"kind":"insert","values":["a","b","c"]}]', "pure insertion")
expect(diff.chars("abc", "").changes.stringify() == '[{"kind":"delete","values":["a","b","c"]}]', "pure deletion")

line_inputs := ["a", "a\n", "a\n\n", "\r\n", "\n", "\r", "a\r\nb\nc\rd", "\r\n\n\r"]
for(text : line_inputs) {
    result := diff.lines(text, text)
    sequence_shape(result, "line endings")
    reconstruct(result, text, text, "line endings")
}
expect(diff.lines("", "\n").changes[0].values.stringify() == '["\\n"]', "no phantom line")
expect(diff.lines("a\r\nb\n", "a\r\nb\r").changes.stringify() == '[{"kind":"equal","values":["a\\r\\n"]},{"kind":"delete","values":["b\\n"]},{"kind":"insert","values":["b\\r"]}]', "exact mixed endings")
expect(diff.lines("a", "a\n").changes.stringify() == '[{"kind":"delete","values":["a"]},{"kind":"insert","values":["a\\n"]}]', "final newline differs")

accent := bytes([195, 169]).decode("utf-8")
cjk := bytes([228, 184, 173]).decode("utf-8")
emoji := bytes([240, 159, 152, 128]).decode("utf-8")
combining := bytes([204, 129]).decode("utf-8")
zwj := bytes([226, 128, 141]).decode("utf-8")
unicode_text := accent + cjk + emoji + "e" + combining + emoji + zwj + emoji
unicode_result := diff.chars(unicode_text, unicode_text + "x")
reconstruct(unicode_result, unicode_text, unicode_text + "x", "Unicode code points")
expect(unicode_result.changes[0].values.size() == 8, "Unicode split code points")
cluster_change := diff.chars("e" + combining + emoji + zwj + emoji, "e" + accent + emoji + zwj + emoji)
expect(cluster_change.changes.size() == 4 && cluster_change.changes[0].values.stringify() == '["e"]' && cluster_change.changes[1].kind == "delete" && cluster_change.changes[1].values[0] == combining && cluster_change.changes[2].kind == "insert" && cluster_change.changes[2].values[0] == accent && cluster_change.changes[3].values.join("") == emoji + zwj + emoji, "combining and ZWJ code-point change")

vt := bytes([11]).decode("utf-8")
ff := bytes([12]).decode("utf-8")
ascii_space := " \t\n" + vt + ff + "\r"
word_result := diff.words("one" + ascii_space + "two", "one two")
sequence_shape(word_result, "words")
reconstruct(word_result, "one" + ascii_space + "two", "one two", "words")
expect(word_result.changes.stringify() == '[{"kind":"equal","values":["one"]},{"kind":"delete","values":[" \\t\\n\\u000b\\f\\r"]},{"kind":"insert","values":[" "]},{"kind":"equal","values":["two"]}]', "ASCII whitespace run")
nbsp := bytes([194, 160]).decode("utf-8")
expect(diff.words("a" + nbsp + "b", "a" + nbsp + "c").changes[0].kind == "delete", "Unicode whitespace is non-whitespace")
unicode_space := diff.words("x" + nbsp + "y", "x y")
expect(unicode_space.changes[0].values.join("") == "x" + nbsp + "y" && unicode_space.changes[0].kind == "delete", "Unicode whitespace exact non-whitespace run")

bad_before := diff.lines(1, "")
sequence_shape(bad_before, "bad before")
expect(bad_before.error_code == "invalid_before_type", "before type code")
expect(diff.lines("", null).error_code == "invalid_after_type", "after type code")
expect(diff.chars([], {}).error_code == "invalid_before_type", "validation order types")

chars8192 := ""
i := 0
while(i < 8192) { chars8192 += "x"; i += 1 }
expect(diff.chars(chars8192, chars8192).ok, "8192 char units")
expect(diff.chars(chars8192 + "x", "").error_code == "before_too_many_units", "8193 char units")
lines8192 := ""
i = 0
while(i < 8192) { lines8192 += "\n"; i += 1 }
expect(diff.lines(lines8192, lines8192).ok, "8192 line units")
expect(diff.lines(lines8192 + "\n", "").error_code == "before_too_many_units", "8193 line units")
max_line_text := "aaaaaaa\n"
i = 0
while(i < 13) { max_line_text += max_line_text; i += 1 }
expect(max_line_text.encode("utf-8").length() == 65536, "maximum line combination construction")
max_line_insert := diff.lines("", max_line_text)
expect(max_line_insert.ok && max_line_insert.changes.size() == 1 && max_line_insert.changes[0].kind == "insert" && max_line_insert.changes[0].values.size() == 8192 && max_line_insert.changes[0].values.join("") == max_line_text, "maximum line combination insertion reconstruction")
max_line_delete := diff.lines(max_line_text, "")
expect(max_line_delete.ok && max_line_delete.changes.size() == 1 && max_line_delete.changes[0].kind == "delete" && max_line_delete.changes[0].values.size() == 8192 && max_line_delete.changes[0].values.join("") == max_line_text, "maximum line combination deletion reconstruction")
max_word_text := "aaaaaaaa        "
i = 0
while(i < 12) { max_word_text += max_word_text; i += 1 }
expect(max_word_text.encode("utf-8").length() == 65536, "maximum word combination construction")
max_word_insert := diff.words("", max_word_text)
expect(max_word_insert.ok && max_word_insert.changes.size() == 1 && max_word_insert.changes[0].kind == "insert" && max_word_insert.changes[0].values.size() == 8192 && max_word_insert.changes[0].values.join("") == max_word_text, "maximum word combination insertion reconstruction")
max_word_delete := diff.words(max_word_text, "")
expect(max_word_delete.ok && max_word_delete.changes.size() == 1 && max_word_delete.changes[0].kind == "delete" && max_word_delete.changes[0].values.size() == 8192 && max_word_delete.changes[0].values.join("") == max_word_text, "maximum word combination deletion reconstruction")
large_line := "x"
i = 0
while(i < 16) { large_line += large_line; i += 1 }
expect(large_line.encode("utf-8").length() == 65536 && diff.lines(large_line, large_line).ok, "65536 byte input")
expect(diff.lines(large_line + "x", "").error_code == "before_too_large", "65537 byte before")
expect(diff.lines("", large_line + "x").error_code == "after_too_large", "65537 byte after")
expect(diff.lines(1, large_line + "x").error_code == "invalid_before_type", "before type precedes after bytes")
expect(diff.lines(large_line + "x", 1).error_code == "invalid_after_type", "after type precedes before bytes")
multibyte65536 := emoji
i = 0
while(i < 14) { multibyte65536 += multibyte65536; i += 1 }
expect(multibyte65536.encode("utf-8").length() == 65536 && diff.lines(multibyte65536, multibyte65536).ok, "65536 byte multibyte input")
expect(diff.lines(multibyte65536 + "x", "").error_code == "before_too_large", "multibyte byte overflow")
old128 := ""
new256 := ""
i = 0
while(i < 256) {
    if(i < 128) { old128 += "a" }
    new256 += "b"
    i += 1
}
expect(diff.chars(old128, new256).ok, "32768 work cells")
expect(diff.chars(old128 + "a", new256).error_code == "work_limit_exceeded", "work cells plus one row")
expect(diff.chars(chars8192 + "x", large_line + "x").error_code == "after_too_large", "byte checks precede unit checks")
expect(diff.chars(chars8192, "").ok && diff.chars("", chars8192).ok, "matrix-free maximum")
common_prefix_text := ""
common_suffix_text := ""
i = 0
while(i < 4095) { common_prefix_text += "p"; i += 1 }
i = 0
while(i < 4096) { common_suffix_text += "s"; i += 1 }
expect(diff.chars(common_prefix_text + "x" + common_suffix_text, common_prefix_text + "y" + common_suffix_text).ok, "max units trimmed to one cell")

first := diff.chars("ab", "ac")
first_changes := first.changes
first_changes[0] = {"kind":"equal","values":["mutated"]}
second := diff.chars("ab", "ac")
expect(!second.stringify().contains("mutated"), "result mutation independence")
nested_values := first.changes[2].values
nested_values.push("nested")
third := diff.chars("ab", "ac")
expect(!third.stringify().contains("nested") && first.changes[0].values.stringify() == '["mutated"]', "nested values mutation independence across calls and chunks")
expect(second.stringify() == diff.chars("ab", "ac").stringify(), "deterministic sequence")

unified_same := diff.unified("a\n", "a\n", {})
expect(unified_same.keys().size() == 4 && unified_same.ok && unified_same.text == "" && unified_same.error == null && unified_same.error_code == null, "unified identical shape")
expect(diff.unified("", "one\n", {}).text == "--- a\n+++ b\n@@ -0,0 +1 @@\n+one\n", "unified empty insertion")
expect(diff.unified("one\n", "", {}).text == "--- a\n+++ b\n@@ -1 +0,0 @@\n-one\n", "unified empty deletion")
expect(diff.unified("a\n", "b\n", {}).text == "--- a\n+++ b\n@@ -1 +1 @@\n-a\n+b\n", "unified replacement")
expect(diff.unified("a", "b", {}).text == "--- a\n+++ b\n@@ -1 +1 @@\n-a\n\\ No newline at end of file\n+b\n\\ No newline at end of file\n", "two no-newline markers")
expect(diff.unified("a", "a\n", {}).text == "--- a\n+++ b\n@@ -1 +1 @@\n-a\n\\ No newline at end of file\n+a\n", "final newline unified")
expect(diff.unified("a\r\n", "b\r", {}).text == "--- a\n+++ b\n@@ -1 +1 @@\n-a\n+b\n", "unified strips source endings")
expect(diff.unified("a\r\n\r\nb\nc\r", "a\r\n\r\nB\nc\r", {}).text == "--- a\n+++ b\n@@ -1,4 +1,4 @@\n a\n \n-b\n+B\n c\n", "unified mixed endings and blank line")
expect(diff.unified("0\n1\n2\n3\n4\n", "0\n1\nX\n3\n4\n", {"context":0,"from":"old","to":"new"}).text == "--- old\n+++ new\n@@ -3 +3 @@\n-2\n+X\n", "context zero and labels")
split := diff.unified("0\n1\n2\n3\n4\n5\n6\n", "0\nX\n2\n3\n4\nY\n6\n", {"context":1}).text
expect(split.contains("@@ -1,3 +1,3 @@\n") && split.contains("@@ -5,3 +5,3 @@\n"), "split hunks")
merged := diff.unified("0\n1\n2\n3\n4\n", "0\nX\n2\nY\n4\n", {"context":1}).text
expect(merged.split("@@").size() == 3, "touching context windows merge")

bad_options := diff.unified(1, 2, [])
expect(!bad_options.ok && bad_options.text == "" && bad_options.error_code == "invalid_options_type", "options validated first")
expect(diff.unified(1, 2, {"extra":true}).error_code == "unknown_option", "unknown before inputs")
expect(diff.unified("", "", {"context":1.5}).error_code == "invalid_context", "float context")
expect(diff.unified("", "", {"context":-1}).error_code == "invalid_context", "negative context")
expect(diff.unified("", "", {"context":101}).error_code == "invalid_context", "large context")
expect(diff.unified("", "", {"from":1}).error_code == "invalid_from_label", "from type")
expect(diff.unified("", "", {"to":"bad\nlabel"}).error_code == "invalid_to_label", "label control")
control_codes := [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 127]
for(code : control_codes) {
    control := bytes([code]).decode("utf-8")
    expect(diff.unified("", "", {"from":"x" + control}).error_code == "invalid_from_label", "from ASCII control")
    expect(diff.unified("", "", {"to":"x" + control}).error_code == "invalid_to_label", "to ASCII control")
}
label256 := ""
i = 0
while(i < 256) { label256 += "l"; i += 1 }
expect(diff.unified("", "", {"from":label256}).ok, "label byte boundary")
expect(diff.unified("", "", {"from":label256 + "l"}).error_code == "invalid_from_label", "label byte overflow")
unicode_label256 := ""
i = 0
while(i < 128) { unicode_label256 += accent; i += 1 }
expect(unicode_label256.encode("utf-8").length() == 256 && diff.unified("", "", {"to":unicode_label256}).ok, "Unicode label byte boundary")
expect(diff.unified("", "", {"to":unicode_label256 + accent}).error_code == "invalid_to_label", "Unicode label byte overflow")
expect(diff.unified(1, "", {}).error_code == "invalid_before_type", "unified input after options")
expect(diff.unified(large_line + "x", 1, {}).error_code == "invalid_after_type", "unified input type precedence")
expect(diff.unified(large_line + "x", "", {}).error_code == "before_too_large", "unified line input limit")
expect(diff.unified(lines8192 + "\n", "", {}).error_code == "before_too_many_units", "unified line unit limit")
expect(diff.unified(old128, new256, {}).ok, "unified line mode avoids char matrix")
old_lines129 := ""
new_lines256 := ""
i = 0
while(i < 129) { old_lines129 += "a\n"; i += 1 }
i = 0
while(i < 256) { new_lines256 += "b\n"; i += 1 }
expect(diff.unified(old_lines129, new_lines256, {}).error_code == "work_limit_exceeded", "unified line work overflow")
large_after := large_line.replace("x", "y")
large_unified := diff.unified(large_line, large_after, {})
expect(large_unified.ok && large_unified.text.encode("utf-8").length() == 131156, "maximum changed single-line unified output")
label215 := ""
i = 0
while(i < 215) { label215 += "l"; i += 1 }
exact_output := diff.unified(large_line, large_after, {"from":label215,"to":label215})
expect(exact_output.ok && exact_output.text.encode("utf-8").length() == 131584, "unified output byte boundary")
over_output := diff.unified(large_line, large_after, {"from":label215 + "l","to":label215})
expect(!over_output.ok && over_output.text == "" && over_output.error_code == "output_too_large", "unified output byte overflow")
expect(diff.unified("a\n", "b\n", {}).stringify() == diff.unified("a\n", "b\n", {}).stringify(), "deterministic unified")

print("PASS diff contract " + count.to_string() + " assertions")
