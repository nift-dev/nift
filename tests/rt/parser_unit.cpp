// CP10 unit tests for the Runtime 2.0 script lexer and parser (src/rt).
// Expected diagnostics are the legacy translator's (src/ParserScript.cpp);
// tests/runtime2/rt_parser_corpus.py checks them against legacy at scale.
#include "rt_parser_tool.h"
#include "rt/lexer.h"

#include <iostream>

using namespace nift::rt;

namespace {

int failures = 0;

void expect(bool ok, const std::string& what) {
    if (ok) return;
    ++failures;
    std::cerr << "FAIL " << what << '\n';
}

std::string dump_of(const std::string& source) {
    Ast ast(source);
    const auto result = parse_script(ast);
    return result.ok ? dump(ast, result.root) : "error " + rt_tool::rt_verdict(source).str();
}

void verdict(const std::string& source, const std::string& expected) {
    const auto got = rt_tool::rt_verdict(source).str();
    expect(got == expected, "verdict of [" + source + "]: got '" + got + "', want '" + expected + "'");
}

void contains(const std::string& source, const std::string& fragment) {
    const auto text = dump_of(source);
    expect(text.find(fragment) != std::string::npos, "dump of [" + source + "] lacks '" + fragment + "':\n" + text);
}

void lexer_tests() {
    const std::string text = "a := 1.5e-2 ?? b?.c ?[0] ... => \"s\\\"q\" 'x' 0x1";
    Lexer lex(text, 0, static_cast<std::uint32_t>(text.size()));
    const Tok want[] = {Tok::Ident, Tok::Declare, Tok::Number, Tok::Coalesce, Tok::Ident, Tok::QuestionDot,
                        Tok::Ident, Tok::QuestionBracket, Tok::Number, Tok::RBracket, Tok::Ellipsis, Tok::Arrow,
                        Tok::String, Tok::String, Tok::Bad, Tok::End};
    for (Tok kind : want) {
        const Token token = lex.next();
        expect(token.kind == kind, std::string("token ") + token_name(token.kind) + " != " + token_name(kind));
    }
    Lexer open("\"abc", 0, 4);
    expect(open.next().kind == Tok::Bad, "unterminated string is Bad");
}

void statement_errors() {
    verdict("fn f() {}", "1:1: fn requires '(name(args))'");
    verdict("fn(f()) x", "1:1: fn requires a block");
    verdict("x := 1\nwhile(true) {", "2:1: function while requires a block");
    verdict("while x {}", "1:1: function while requires '(...)'");
    verdict("if x {}", "1:1: function if requires '(...)'");
    verdict("if (x {}", "1:1: function if has no matching ')'");
    verdict("if (x) y", "1:1: function if requires a block");
    verdict("if (x) {} else if x {}", "1:10: function else if malformed");
    verdict("if (x) {} else if (x) y", "1:10: function else if requires block");
    verdict("if (x) {}\nelse y", "1:10: function else requires block");
    verdict("for x {}", "1:1: function for requires '(...)'");
    verdict("for (x : y) z", "1:1: function for requires a block");
    verdict("try {", "1:1: try requires a balanced block");
    verdict("try {} x", "1:1: try requires catch(identifier)");
    verdict("try {} catch e {}", "1:1: catch requires '(identifier)'");
    verdict("try {} catch (1) {}", "1:1: catch requires one valid identifier");
    verdict("try {} catch (e) x", "1:1: catch(identifier) requires a block");
    verdict("enum {}", "1:1: enum requires 'Name { members }'");
    verdict("struct S {}", "1:1: struct requires '(name)'");
    verdict("struct(S) x", "1:1: struct requires a block");
    verdict("export x", "1:1: export requires '(binding)'");
    verdict("x := 1 /* open", "ok");
    verdict("/* open", "1:1: open comment '/*' has no close '*/'");
    verdict("@/* open", "1:1: open comment '@/*' has no close '*/'");
    verdict("throw ;", "1:1: throw requires an Error expression");
    verdict("fn(f()) {\n  if (x) {\n    while y {}\n  }\n}", "3:5: function while requires '(...)'");
    verdict("x := [1,\n 2]\n  if (a) { } else\n", "3:13: function else requires block");
    // find_balanced skips <!-- --> and @/* */ (review mutation: removing them)
    verdict("while (x) { <!-- } -->", "1:1: function while requires a block");
    verdict("while (x) { @/* } */", "1:1: function while requires a block");
}

void accepted_forms() {
    contains("x := 1 + 2 * 3", "Binary + @5..14");
    contains("x := 1 + 2 * 3", "Binary * @9..14");
    contains("const x := 1", "Declare flags=1");
    contains("immut x := 1", "Declare flags=2");
    contains("x += 2", "CompoundAssign +=");
    contains("x++", "IncDec ++post");
    contains("++x", "IncDec ++pre");
    contains("a[0][1] = 9", "Assign");
    contains("o.f.g = 1", "Assign");
    contains("y := a ?? b ?? c", "Coalesce @5..16\n      Ident @5..6 `a`\n      Coalesce @10..16");
    contains("y := 1 == 1 == true", "Binary == @5..19\n      Number @5..6 `1`\n      Binary == @10..19");
    contains("y := a - b - c", "Binary - @5..14\n      Binary - @5..10");
    contains("y := -a.b", "Unary neg @5..9\n      Member @6..9");
    contains("print(c ? 1 : 2)", "Ternary");
    contains("return c ? 1 : 2", "Ternary");
    contains("f := (a, ...r) => a", "Param flags=8 @12..13 `r`");
    contains("f := a => a + 1", "Lambda @5..15");
    contains("f := async () => 1", "Lambda flags=4");
    contains("f := (a) => {\n return a\n}", "Lambda flags=16");
    contains("fn[async](g()) { return 1 }", "FnDecl flags=4");
    contains("print(...xs)", "Spread");
    contains("x := await f()", "Await");
    contains("await f()", "Await");
    contains("s := \"a $[x + 1] b\"", "StringPart @6..8 `a `");
    contains("for((k, v) : o) { break }", "ForBinding flags=256");
    contains("for(i : range(3)) { continue }", "Continue");
    contains("struct(P) {\n  x := 1\n  private y := 2\n  fn(m(a)) { return a }\n}", "StructField flags=32");
    contains("enum Color { Red, Green }", "EnumDecl");
    contains("import(\"m.f\")", "Import");
    contains("export(x)", "Export");
    contains("git status", "Command @0..10");
    contains("./tool --x", "Command");
    contains("@print(1)", "TemplateStmt");
    contains("x := 1 // trailing", "Opaque [legacy-runtime-error]");
    contains("x := true ? 1 : 2", "Opaque [irregular-legacy-parse]");
    contains("o := {a: 1}", "Opaque [legacy-runtime-error]");
    contains("x := 0x10", "Opaque [legacy-runtime-error]");
    contains("for(i := 0; i < 3; i++) { }", "Opaque [legacy-runtime-error]");
    contains("f := (a) => { while x {} }", "Opaque [deferred-translation]");
    contains("fn(f(a, a)) { }", "Opaque [legacy-runtime-error]");
    contains("throw := 1", "Declare");
    contains("break x", "Command @0..7");
    contains("#! comment", "Comment");
    contains("x := atomic<int>(10)", "Ident @5..16 `atomic<int>`");
    contains("print(x++)", "IncDec ++post @6..9");
    contains("f := () => n--", "IncDec --post");
    contains("y := x++ + 1", "Opaque [irregular-legacy-parse]");
    contains("x &= 6", "CompoundAssign &=");
    contains("x ^= 3", "CompoundAssign ^=");
    contains("[a, b] := pair", "Pattern @0..6");
    contains("{a, b} = o", "Pattern flags=2048");
    contains("[a, a] := pair", "Opaque [legacy-runtime-error]");
    contains("print(\"abc\".length())", "Member @6..18");
    contains("@fn[async](g(n)) {\n  return n\n}", "FnDecl flags=4100");
    contains("@fn(h()) { while x {} }", "Opaque [deferred-translation]");
    contains("@struct(P) { x := 1 }", "StructDecl");
    contains("@import(\"m.f\")", "Import");
    contains("$[x := 1]", "Declare @2..8");
    contains("@fn(h()) { } trailing", "TemplateStmt");
    contains("f := (x) => { <i>$[x]</i> }", "Opaque [template-text]");
    contains("x := " + std::string(500, '(') + "1" + std::string(500, ')'), "Opaque [nesting-limit]");
}

// Adversarial review of CP10 (cp10-parser.md, Reviews): every construct legacy
// rejects or reads differently at run time is Opaque.
void review_findings() {
    const std::string lre = "Opaque [legacy-runtime-error]", irr = "Opaque [irregular-legacy-parse]";
    // D1/D2 await
    contains("print(await f() + 1)", irr);
    contains("x := await f() * 2", irr);
    contains("print(await(f()))", lre);
    contains("x := await\tf()", lre);
    contains("await := 1", "Opaque");
    contains("print(1 + await f())", "Await");
    contains("print(!await f())", "Await");
    contains("x := await f()", "Await @5..14");
    // D3 multi-line #! statement
    contains("#! note (\n$[print(1)]\n)", "Opaque [template-text]");
    contains("#! note", "Comment");
    // D4 escapes and $ in one string; naive `]` matching in $[...]
    contains("print(\"\\\\$[1+1]\")", irr);
    contains("print(\"\\n$[1]\")", irr);
    contains("print(\"a\\$b\")", irr);
    contains("print('$[\"]\"]')", lre);
    contains("print(\"\\q\")", "String @6..10");
    // D5 postfix and call shapes
    for (const char* text : {"print(x [0])", "print(o . a)", "print(o.a .b)", "print(o?. a)", "print(o.a\n.b)",
                             "print(\"abc\"[1])", "print('r'.k)", "print(1e2[0])", "print(true.k)",
                             "print(true.length())", "print(((x) => x)(1))", "print((f)(1))", "print(mk()(1))",
                             "print(g[0](false))", "print(xs[0].f(5))", "print(o.a[0](3))", "print(f (1))",
                             "print(xs[xs[0]])", "print(f( ))", "print(s.split(1).stringify(1))",
                             "print(\"a\".stringify( ))", "print((xs)[0])",
                             "print(1 + x => x)", "print((x) => {x} + 1)"})
        contains(text, "Opaque");
    contains("print(o.f(3))", "Call @6..12");
    contains("print(o?.m.g(3))", "Call");
    contains("print(xs[0].to_string())", "Call");
    contains("print((1 + 2).to_string())", "Call @6..25");
    contains("print(g(o).a)", "Member");
    contains("print({\"a\": 1}.a)", "Member @6..16");
    contains("print([1][0])", "Index @6..12");
    contains("print(s.split(\"-\").stringify())", "Member @6..28");
    contains("print(\"a\".prettify())", "Call");
    // D6 trailing commas
    for (const char* text : {"print([1, 2,])", "print({\"a\": 1,})", "print(f(1,))", "[a,] := xs",
                             "f := (a,) => a"})
        contains(text, lre);
    // D7 declaration checks
    for (const char* text : {"enum E { A, A }", "enum E { }", "struct (P) { fn(P(...a)) { } }",
                             "struct (P) {\nfn(P()) { }\nfn(P(a)) { }\n}", "fn(f( )) { }", "f := ( ) => 1",
                             "o := {\"a\": 1, \"a\": 2}"})
        contains(text, "Opaque");
    contains("enum E { A, }", "EnumDecl");
    // D8 nesting: bounded well below legacy's 96
    contains("x := " + std::string(40, '[') + "1" + std::string(40, ']'), "Array");
    contains("x := " + std::string(70, '[') + "1" + std::string(70, ']'), "Opaque [nesting-limit]");
    contains("x := s" + [] { std::string t; for (int i = 0; i < 70; ++i) t += ".to_string()"; return t; }(),
             "Opaque [nesting-limit]");
    // D9 spans cover closing parentheses
    contains("x := 2 * (1 + 3)", "Binary * @5..16");
    contains("y := -(1)", "Unary neg @5..9");
    // D10 @import vs import
    contains("@import(\"m.f\")", "Import flags=8192");
    contains("import(\"m.f\")", "Import @0..13");
    // R2 escapes and interpolation in object keys and verbatim values
    contains("o := {\"a\\tb\": 1}", lre);
    contains("o := {\"a\": \"$[x]\"}", lre);
    contains("o := [\"$[x]\"]", lre);
    // `!` binds looser than arithmetic in legacy
    contains("print(!1 + 1)", irr);
    contains("print(!a == b)", "Binary ==");
    // equality chains in conditions
    contains("while (a != b == c) { }", irr);
    contains("while (a != (b == c)) { }", "While");
    // lambdas in if conditions
    contains("if ((p) => 1) { }", irr);
}

void locations() {
    const auto loc = locate("ab\ncd\n", 4);
    expect(loc.line == 2 && loc.column == 2, "locate line/column");
    expect(locate("", 0).line == 1, "locate empty");
}

} // namespace

int main() {
    lexer_tests();
    statement_errors();
    accepted_forms();
    review_findings();
    locations();
    if (failures) {
        std::cerr << failures << " rt parser unit failure(s)\n";
        return 1;
    }
    std::cout << "PASS rt parser unit\n";
    return 0;
}
