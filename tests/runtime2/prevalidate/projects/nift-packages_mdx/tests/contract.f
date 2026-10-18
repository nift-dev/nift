@import("mdx")

fn(expect(condition, label)) { if(!condition) { print("FAIL " + label); missing.value } }

basic := mdx.parse("# hello\n\n<Component enabled title=\"a > { b\" value={{x: 1}}>Hi {name}</Component>\n")
expect(basic.ok, "basic parse")
expect(basic.keys().join(",") == "ok,source,path,frontmatter,imports,exports,nodes,dependencies,diagnostics", "result shape")
expect(basic.frontmatter.keys().join(",") == "present,source,body,start,end", "frontmatter shape")
expect(basic.path == null && basic.dependencies.length() == 0, "parse purity shape")
expect(basic.nodes.length() == 3 && basic.nodes[0].type == "markdown" && basic.nodes[1].type == "jsx_element", "basic nodes")
element := basic.nodes[1]
expect(element.keys().join(",") == "type,source,name,value,attributes,children,position", "node shape")
expect(element.position.keys().join(",") == "start,end" && element.position.start.keys().join(",") == "offset,line,column", "position shape")
expect(element.name == "Component" && element.attributes.length() == 3 && element.children.length() == 2, "JSX shape")
expect(element.attributes[0].keys().join(",") == "name,kind,value,source,position", "attribute shape")
expect(element.attributes[0].kind == "boolean" && element.attributes[1].value == "a > { b" && element.attributes[2].kind == "expression", "JSX attributes")
expect(element.children[1].type == "expression" && element.children[1].value == "name", "JSX expression child")

front := mdx.parse("---\r\ntitle: raw\r\n---\r\nbody")
expect(front.ok && front.frontmatter.present && front.frontmatter.body == "title: raw\r\n", "raw frontmatter")
expect(front.frontmatter.start.offset == 0 && front.frontmatter.end.offset == 22, "frontmatter positions")
expect(front.nodes.length() == 1 && front.nodes[0].source == "body", "frontmatter excluded from nodes")

esm := mdx.parse("import React, {x as y} from './a.mdx';\nimport './b.md'\nexport {x} from './x.mdx'\n")
expect(esm.ok && esm.imports.length() == 2 && esm.exports.length() == 1, "ESM records")
expect(esm.imports[0].keys().join(",") == "kind,specifier,source,local,extension,dependency,position", "import shape")
expect(esm.exports[0].keys().join(",") == "source,specifier,position", "export shape")
expect(esm.imports[0].specifier == "./a.mdx" && esm.imports[0].kind == "import" && esm.imports[0].dependency, "from import")
expect(esm.imports[1].specifier == "./b.md" && esm.imports[1].kind == "side_effect" && esm.imports[1].dependency, "side effect import")
expect(esm.exports[0].specifier == "./x.mdx", "re-export retained")

esm_boundaries := mdx.parse("import './first.mdx'\nexport {x}\n\nimport './second.mdx'\nparagraph\nimport './not-esm.mdx'\n")
expect(esm_boundaries.ok && esm_boundaries.imports.length() == 2 && esm_boundaries.exports.length() == 1, "ESM paragraph boundaries")
front_esm := mdx.parse("---\r\nx: y\r\n---\r\nimport './front.mdx'\r\n")
expect(front_esm.ok && front_esm.imports.length() == 1, "ESM after frontmatter")

slashes := mdx.parse("{typeof /}/.source}{a++ / b + ({c:1}).c}")
expect(slashes.ok && slashes.nodes.length() == 2 && slashes.nodes[0].source == "{typeof /}/.source}" && slashes.nodes[1].source == "{a++ / b + ({c:1}).c}", "slash lexical state")

commented_import := mdx.parse("import /* lead */ x from /* source */ './commented.mdx'\nimport /* side */ './side.mdx'\n")
expect(commented_import.ok && commented_import.imports.length() == 2 && commented_import.imports[0].specifier == "./commented.mdx" && commented_import.imports[1].specifier == "./side.mdx", "import comments")
line_commented_import := mdx.parse("import // lead\n './line-side.mdx'\nimport x from // source\n './line-from.mdx'\n")
expect(line_commented_import.ok && line_commented_import.imports.length() == 2 && line_commented_import.imports[0].specifier == "./line-side.mdx" && line_commented_import.imports[1].specifier == "./line-from.mdx", "import line comments")
line_commented_export := mdx.parse("export // lead\n const x = 1\nexport default // value\n 1\n")
expect(line_commented_export.ok && line_commented_export.exports.length() == 2 && line_commented_export.exports[0].source == "export // lead\n const x = 1\n" && line_commented_export.exports[1].source == "export default // value\n 1\n", "export line comments")
completed_export_comment := mdx.parse("export const x = 1 // done\nparagraph\n")
expect(completed_export_comment.ok && completed_export_comment.exports.length() == 1 && completed_export_comment.exports[0].source == "export const x = 1 // done\n" && completed_export_comment.nodes.length() == 2, "completed export comment boundary")
completed_default_comment := mdx.parse("export default 1 // done\nimport './after-default.mdx'\n")
expect(completed_default_comment.ok && completed_default_comment.exports.length() == 1 && completed_default_comment.imports.length() == 1 && completed_default_comment.imports[0].specifier == "./after-default.mdx", "completed default export boundary")
escaped_import := mdx.parse("import './quoted\\\"name.mdx'\n")
expect(escaped_import.ok && escaped_import.imports[0].specifier == "./quoted\"name.mdx", "safe specifier escape")

control_regex := "{(() => { if (x) /}/.test(y); while (x) /[a}]+/g.test(y); for (;x;) /z}/.test(y); switch (x) { case 1: break } /q}/.test(y); try {} catch (e) {} /r}/.test(y) })()}"
control_regex_result := mdx.parse(control_regex)
expect(control_regex_result.ok && control_regex_result.nodes.length() == 1 && control_regex_result.nodes[0].source == control_regex, "control-flow regex literals")

shielded := mdx.parse("```mdx\n<X>{bad}</X>\n```\n`<Y>{also}</Y>`\n{`x ${/}/.test(v) ? {a: 1} : {}}`}\n")
expect(shielded.ok && shielded.nodes.length() == 5, "shielded constructs")
expect(shielded.nodes[0].type == "code_fence" && shielded.nodes[1].type == "inline_code" && shielded.nodes[3].type == "expression", "shielded node types")

unicode_source := bytes([99,97,102,195,169,32,240,159,152,128,32,123,118,97,108,117,101,125,10]).decode("utf-8")
unicode := mdx.parse(unicode_source)
expect(unicode.ok && unicode.source == unicode_source, "Unicode preserved")
expect(unicode.nodes[1].position.start.offset == 11 && unicode.nodes[1].position.start.column == 12, "UTF-8 byte position")

bad := mdx.parse("before <A><B /></C> after")
expect(!bad.ok && bad.nodes.length() == 0 && bad.imports.length() == 0 && bad.exports.length() == 0, "structural failure clears AST")
expect(bad.source == "before <A><B /></C> after" && bad.diagnostics[0].code == "mismatched_jsx", "structural failure source and diagnostic")
expect(bad.diagnostics[0].keys().join(",") == "code,message,severity,path,line,column,offset", "diagnostic shape")

invalid := mdx.parse(7)
expect(!invalid.ok && invalid.source == null && invalid.diagnostics[0].code == "invalid_type", "invalid source type")

baseline := basic.stringify()
i := 0
while(i < 10) { expect(mdx.parse(basic.source).stringify() == baseline, "determinism"); i += 1 }

print("PASS mdx contract")
