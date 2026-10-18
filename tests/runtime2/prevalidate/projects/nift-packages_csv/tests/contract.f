@import("csv")

fn(expect(condition, label)) { if(!condition) { print("FAIL " + label); missing.value } }

defaults := csv.options()
expect(defaults.delimiter == "," && defaults.quote == "\"" && defaults.newline == "\r\n", "defaults")
expect(csv.parse("").stringify() == "[]", "empty input")
expect(csv.parse("\r\n").stringify() == "[[]]", "blank record")
expect(csv.parse("\"\"").stringify() == "[[\"\"]]", "explicit empty field")
expect(csv.parse("a,").stringify() == "[[\"a\",\"\"]]", "trailing field")
expect(csv.parse("a,b\r\nc,d").stringify() == "[[\"a\",\"b\"],[\"c\",\"d\"]]", "CRLF records")
expect(csv.parse("\"a,b\",\"line1\r\nline2\",\"a\"\"b\"").stringify() == "[[\"a,b\",\"line1\\r\\nline2\",\"a\\\"b\"]]", "quoted content")
expect(csv.parse_bytes("caf\u00e9,tea".encode("utf-8")).stringify() == "[[\"caf\u00e9\",\"tea\"]]", "strict UTF-8 bytes")

quoted_lines := [["bare\rcr", "bare\nlf", "pair\r\nend"]]
quoted_line_text := csv.stringify_document({"rows":quoted_lines}, {"accept_lf":false})
quoted_line_document := csv.parse_document(quoted_line_text, {"accept_lf":false})
expect(quoted_line_document.ok && quoted_line_document.rows.stringify() == quoted_lines.stringify(), "quoted CR LF and CRLF")

tab := {"delimiter":"\t","quote":"\"","newline":"\n","accept_lf":true,"bom":false,"header":false,"strict_widths":false}
expect(csv.parse_document("a\tb\nc\td", tab).rows.stringify() == "[[\"a\",\"b\"],[\"c\",\"d\"]]", "custom delimiter")
expect(csv.stringify_document({"rows":[["a", "b"], ["c", "d"]]}, tab) == "a\tb\nc\td", "custom output")
expect(csv.stringify_document({"rows":[["a;b", "x'y"]]}, {"delimiter":";","quote":"'","line_ending":"\n"}) == "'a;b';'x''y'", "custom quote and line-ending alias")

lf_rejected := csv.parse_document("a,b\nc,d", {"accept_lf":false})
expect(!lf_rejected.ok && lf_rejected.diagnostics[0].code == "bare_lf", "reject LF")
expect(csv.parse_document("a,b\nc,d", {"accept_lf":true}).ok, "accept LF")

bom := bytes([239, 187, 191]).decode("utf-8")
expect(!csv.parse_document(bom + "a,b", {}).ok, "disabled BOM")
expect(csv.parse_document(bom + "a,b", {"bom":true}).rows.stringify() == "[[\"a\",\"b\"]]", "enabled BOM")
expect(csv.stringify_document({"rows":[["a"]]}, {"bom":true}).starts_with(bom), "emit BOM")

headed := csv.parse_document("name,name,age\r\nA,B,1", {"header":true,"strict_widths":true})
expect(headed.ok && headed.headers.size() == 3 && headed.rows.size() == 1, "header parse")
expect(headed.diagnostics.size() == 1 && headed.diagnostics[0].code == "duplicate_header", "duplicate header")
expect(headed.diagnostics[0].message == "duplicate header" && !headed.diagnostics[0].message.contains("name"), "generic duplicate header diagnostic")
expect(!csv.parse_document("a,b\r\n1", {"header":true,"strict_widths":true}).ok, "strict header width")
expect(!csv.parse_document("a,b\r\n1,2,3", {"strict_widths":true}).ok, "strict first width")
expect(csv.parse_document("A,1", {"header":["name","age"],"strict_widths":true}).headers.stringify() == "[\"name\",\"age\"]", "supplied headers")
expect(csv.stringify_document({"headers":["name","age"],"rows":[{"name":"Ada","age":"37"}]}, {"header":true}) == "name,age\r\nAda,37", "object rows")
expect(csv.stringify_document({"headers":["name","age"],"rows":[["Ada","37"]]}, {"strict_widths":true}) == "Ada,37", "strict stringify with headers")
expect(csv.stringify_document({"rows":[["a","b"],["c","d"]]}, {"strict_widths":true}) == "a,b\r\nc,d", "strict stringify with first row")
expect(csv.stringify_document({"headers":[],"rows":[{}]}, {}) == "\r\n", "empty headers permit empty object row")
supplied_header_options := {"header":["name","age"],"strict_widths":true}
supplied_header_text := csv.stringify_document({"rows":[{"name":"Ada","age":"37"}]}, supplied_header_options)
supplied_header_document := csv.parse_document(supplied_header_text, supplied_header_options)
expect(supplied_header_text == "Ada,37" && supplied_header_document.rows.stringify() == "[[\"Ada\",\"37\"]]", "supplied header same-options round trip")

for(bad : ["\"open", "a\"b,c", "\"a\"x,b", "a\rb"]) {
    expect(!csv.parse_document(bad, {}).ok, "malformed quote transition")
}

round_rows := [[], [""], ["", ""], [], ["a", ""], ["a,b", "x\"y", "line1\nline2"], ["plain"], [""]]
encoded := csv.stringify(round_rows)
expect(csv.parse(encoded).stringify() == round_rows.stringify(), "row round trip")
document := csv.parse_document("a,b\r\nc,d\r\n", {})
expect(document.terminal_newline && csv.stringify_document(document, {}) == "a,b\r\nc,d\r\n", "terminal newline")
expect(csv.stringify(round_rows) == encoded && csv.stringify(round_rows) == encoded, "repeat deterministic")

large := "x"
i := 0
while(i < 16) { large += large; i += 1 }
large_rows := []
i = 0
while(i < 1024) { large_rows.push([i.to_string(), "value", ""]); i += 1 }
wide := []
i = 0
while(i < 2048) { wide.push("field" + i.to_string()); i += 1 }
boundary_document := csv.parse_document(large, {})
expect(boundary_document.ok && boundary_document.rows[0][0] == large, "65536-byte parse boundary admitted")
expect(csv.stringify([[large]]) == large, "65536-byte stringify boundary admitted")
over_boundary := csv.parse_document(large + "x", {})
expect(!over_boundary.ok && over_boundary.rows.size() == 0 && over_boundary.headers.size() == 0, "65537-byte parse rejected")
expect(over_boundary.diagnostics.size() == 1 && over_boundary.diagnostics[0].code == "input_too_large", "oversize diagnostic")
expect(csv.parse(csv.stringify([wide]))[0].size() == 2048, "wide row")
expect(csv.parse(csv.stringify(large_rows)).size() == 1024, "large row count and input")

max_fields := []
i = 0
while(i < 4096) { max_fields.push(""); i += 1 }
max_fields_text := csv.stringify([max_fields])
expect(csv.parse_document(max_fields_text, {}).rows[0].size() == 4096, "4096-field boundary admitted")
too_many_fields := csv.parse_document(max_fields_text + ",", {})
expect(!too_many_fields.ok && too_many_fields.diagnostics[0].code == "too_many_fields", "4097 fields rejected")
cell_boundary_rows := [max_fields, max_fields, max_fields, max_fields]
cell_boundary_text := csv.stringify(cell_boundary_rows)
cell_boundary_document := csv.parse_document(cell_boundary_text, {})
expect(cell_boundary_document.ok && cell_boundary_document.rows.size() == 4, "16384-cell array and parse boundary admitted")
too_many_cells := csv.parse_document(cell_boundary_text + "\r\nx", {})
expect(!too_many_cells.ok && too_many_cells.diagnostics[0].code == "too_many_cells", "16385 parsed cells rejected")

max_records := []
i = 0
while(i < 4096) { max_records.push([]); i += 1 }
max_records_text := csv.stringify(max_records)
expect(csv.parse_document(max_records_text, {}).rows.size() == 4096, "4096-record boundary admitted")
too_many_records := csv.parse_document(max_records_text + "\r\n", {})
expect(!too_many_records.ok && too_many_records.diagnostics[0].code == "too_many_records", "4097 records rejected")

max_headers := []
i = 0
while(i < 4096) { max_headers.push(i.to_string()); i += 1 }
max_header_document := csv.parse_document("", {"header":max_headers})
expect(max_header_document.ok && max_header_document.headers.size() == 4096, "4096-header boundary admitted")
max_headers.push("extra")
too_many_headers := csv.parse_document("", {"header":max_headers})
expect(!too_many_headers.ok && too_many_headers.diagnostics[0].code == "too_many_headers", "4097 headers rejected")

path := "csv-contract-output.csv"
expect(csv.write(path, [["a,b", "c"]]), "write")
expect(csv.read(path).stringify() == "[[\"a,b\",\"c\"]]", "read")
expect(csv.write_document(path, {"headers":["a","b"],"rows":[["1","2"]]}, {"header":true}), "write document")
expect(csv.read_document(path, {"header":true}).rows.stringify() == "[[\"1\",\"2\"]]", "read document")
file(path).remove()

print("PASS csv package contract")
