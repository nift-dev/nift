#include <minify/Minify.h>
#include <cstdlib>
#include <iostream>
#include <string>

static void expect(bool ok, const std::string& message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static void eq(const std::string& got, const std::string& want, const char* label) {
    if (got != want) {
        std::cerr << label << "\nwant: [" << want << "]\ngot:  [" << got << "]\n";
        std::exit(1);
    }
}

int main() {
    std::string out, err;

    expect(minify::json(R"( { "a" : 1, "s" : "a b", "x" : [ true, null ] } )", out, err), err);
    eq(out, R"({"a":1,"s":"a b","x":[true,null]})", "json compact");
    expect(!minify::json(R"({"a":})", out, err), "invalid JSON accepted");
    expect(err.find("invalid JSON") != std::string::npos, "invalid JSON error missing");

    expect(minify::css("/*x*/ body  { color : red ; margin : 0  10px ; }", out, err), err);
    eq(out, "body{color:red;margin:0 10px;}", "css basic");
    expect(minify::css("/*!license*/ .x { content: \"a  b\"; }", out, err), err);
    expect(out.find("/*!license*/") != std::string::npos, "CSS license comment removed");
    expect(out.find("\"a  b\"") != std::string::npos, "CSS string whitespace changed");
    expect(minify::css("@laye/ *{.a{value:* /}}", out, err), err);
    expect(out.find("/ *") != std::string::npos, "CSS whitespace removal created a comment opener");
    expect(out.find("* /") != std::string::npos, "CSS whitespace removal created a comment closer");

    // CSS Syntax treats EOF as recovery for unterminated comments/strings and
    // an unescaped newline as the end of a bad-string token. Minification must
    // not reject or retokenize source that browsers intentionally recover.
    expect(minify::css(".a { color: red; } /* eof", out, err), err);
    eq(out, ".a{color:red;}", "css eof comment recovery");
    expect(minify::css("/*!license eof", out, err), err);
    eq(out, "/*!license eof", "css preserved eof comment recovery");
    expect(minify::css(".a { content: \"unterminated", out, err), err);
    eq(out, ".a{content:\"unterminated", "css eof string recovery");
    expect(minify::css(".a { content: \"bad\n; color: red; }", out, err), err);
    expect(out.find("\n") != std::string::npos, "CSS bad-string terminating newline removed");
    expect(out.find("color:red") != std::string::npos, "CSS after bad-string newline was swallowed");

    // Authored CSS whitespace can separate tokens even when neither side is an
    // identifier character. These are public semantic contracts, not preferred
    // output spellings: removing the spaces changes or invalidates the CSS.
    expect(minify::css(".grid { grid-template-columns: 1.15fr .85fr; font: 700 .75rem sans-serif; padding: .1em .3em; }", out, err), err);
    expect(out.find("1.15fr .85fr") != std::string::npos,
           "CSS dimension whitespace removed before leading decimal");
    expect(out.find("700 .75rem") != std::string::npos,
           "CSS font whitespace removed before leading decimal");
    expect(out.find(".1em .3em") != std::string::npos,
           "CSS value-list whitespace removed before leading decimal");
    expect(minify::css(".a .b, .a #id, .a :hover, .a [data-x], .a * { color: red; }", out, err), err);
    expect(out.find(".a .b") != std::string::npos, "CSS descendant class selector merged");
    expect(out.find(".a #id") != std::string::npos, "CSS descendant ID selector merged");
    expect(out.find(".a :hover") != std::string::npos, "CSS descendant pseudo selector merged");
    expect(out.find(".a [data-x]") != std::string::npos, "CSS descendant attribute selector merged");
    expect(out.find(".a *") != std::string::npos, "CSS descendant universal selector merged");
    expect(minify::css("@media screen and (width > 10px) { .x { transform: translateX(1px) scale(2); color: color-mix(in srgb, var(--bg) 92%, transparent); } }", out, err), err);
    expect(out.find("and (") != std::string::npos, "CSS media condition whitespace removed");
    expect(out.find(") scale(") != std::string::npos, "CSS transform-list whitespace removed");
    expect(out.find("var(--bg) 92%") != std::string::npos,
           "CSS color percentage whitespace removed");
    expect(minify::css("@media (prefers-color-scheme: dark) { .x { color: red; } }", out, err), err);
    expect(out.find("@media (") != std::string::npos,
           "CSS at-rule name merged with parenthesized prelude");
    expect(minify::css(R"CSS(.fonts { font-family: "A B" serif; content: "a" "b"; } * .item, [data-x] button { color: red; })CSS", out, err), err);
    expect(out.find("\"A B\" serif") != std::string::npos,
           "CSS quoted font-family boundary merged");
    expect(out.find("\"a\" \"b\"") != std::string::npos,
           "CSS adjacent string values merged");
    expect(out.find("* .item") != std::string::npos,
           "CSS universal descendant selector merged");
    expect(out.find("] button") != std::string::npos,
           "CSS attribute descendant type selector merged");

    // WPT-derived regressions: native nesting descendant combinators,
    // escaped selector whitespace and namespace separator whitespace must
    // survive when removing it changes the browser's parsed rule tree.
    expect(minify::css(".a { & .b { color: red; } & span { color: blue; } &::before { content: \"x\"; } }", out, err), err);
    expect(out.find("& .b") != std::string::npos, "CSS nesting descendant class merged into parent selector");
    expect(out.find("& span") != std::string::npos, "CSS nesting descendant type merged into parent selector");
    expect(out.find("&::before") != std::string::npos, "CSS nesting compound pseudo selector unnecessarily separated");
    expect(minify::css(".a { .ancestor & { color: red; } }", out, err), err);
    expect(out.find(".ancestor &") != std::string::npos, "CSS nesting right-side descendant parent selector merged");
    expect(minify::css(R"CSS(::part(\(foo) {} ::part(   bar\    ) {} ::part( -foo  bar    ) {})CSS", out, err), err);
    expect(out.find("bar\\ ") != std::string::npos, "CSS escaped selector whitespace removed");
    expect(minify::css("[ |data-test-4] { color: green; } [ | data-test-4] { color: red; }", out, err), err);
    expect(out.find("[ |data-test-4]") != std::string::npos, "CSS attribute namespace leading whitespace removed");
    expect(out.find("[ | data-test-4]") != std::string::npos, "CSS invalid attribute namespace whitespace normalized into valid selector");

    // WPT-derived: a CSS hex escape consumes one trailing whitespace as its
    // terminator, and an escaped whitespace is an identifier character. The
    // whitespace run after either therefore cannot collapse to a single space:
    // browsers tokenize `\2020  \2021` as two symbols but `\2020 \2021` as one.
    expect(minify::css(R"CSS(@counter-style a { symbols: \2020  \2021; suffix: ""; })CSS", out, err), err);
    expect(out.find(R"CSS(\2020  \2021)CSS") != std::string::npos, "CSS hex escape terminator+separator spaces collapsed");
    expect(minify::css(R"CSS(@counter-style b { symbols: \   x; suffix: ""; })CSS", out, err), err);
    expect(out.find(R"CSS(\  x)CSS") != std::string::npos, "CSS escaped-space separator collapsed");
    expect(minify::css(R"CSS(@counter-style c { symbols: a\0304  a\0301; suffix: ""; })CSS", out, err), err);
    expect(out.find(R"CSS(a\0304  a\0301)CSS") != std::string::npos, "CSS hex escape run after identifier collapsed");
    expect(minify::css(R"CSS(@counter-style d { additive-symbols: \66  6, 'e' 5; })CSS", out, err), err);
    expect(out.find(R"CSS(\66  6)CSS") != std::string::npos, "CSS hex escape additive-symbols weight separator collapsed");

    // Invalid declaration values must stay invalid after compaction. Browsers
    // discard these declarations; joining a block with adjacent tokens can
    // accidentally make the declaration survive.
    expect(minify::css(".a { color: rgb(2,2,2); color:var(--x) { }; background:red; }", out, err), err);
    expect(out.find("var(--x) {}") != std::string::npos, "CSS invalid declaration block boundary changed");
    expect(minify::css(".a { color: rgb(2,2,2); color:{ } var(--x); background:red; }", out, err), err);
    expect(out.find("{} var(--x)") != std::string::npos, "CSS invalid declaration block/value boundary changed");

    // A bad string ending at the final newline is different from an EOF-ended
    // string. Preserve the newline so the browser performs the same recovery
    // and still discards the malformed declaration.
    expect(minify::css("p { color: green; color: var(--a, \"\n", out, err), err);
    expect(!out.empty() && out.back() == '\n', "CSS final bad-string newline trimmed");
    expect(minify::css("p { color: green; color: var(--a, url(\"\n", out, err), err);
    expect(!out.empty() && out.back() == '\n', "CSS final bad-url string newline trimmed");

    expect(minify::html("  <div   class=\"a  b\">  hello   world <!-- gone --> <span> x </span> </div>  ", out, err), err);
    eq(out, "<div class=\"a  b\"> hello world <span> x </span> </div>", "html basic");
    expect(minify::html("<pre>  a\n    b </pre><script> const x = ` a  b `;\n</script>", out, err), err);
    expect(out.find("<pre>  a\n    b </pre>") != std::string::npos, "pre contents changed");
    expect(out.find("<script> const x = ` a  b `;\n</script>") != std::string::npos, "script contents changed");
    expect(minify::html("a<!--[if IE]>x<![endif]-->b", out, err), err);
    expect(out.find("<!--[if IE]>") != std::string::npos, "conditional comment removed");

    expect(minify::javascript("const  x = 1; // comment\nconst y = x + 2;\n", out, err), err);
    eq(out, "const x=1;const y=x+2", "JavaScript delimited newline removal");
    expect(out.find("// comment") == std::string::npos, "JS line comment retained");
    expect(minify::javascript("const r = /https?:\\/\\/example\\.com/; /*x*/\nconst t=` a  b `;", out, err), err);
    expect(out.find("/https?:\\/\\/example\\.com/") != std::string::npos, "JS regex damaged");
    expect(out.find("` a  b `") != std::string::npos, "JS template damaged");
    expect(minify::javascript("return\n  value;", out, err), err);
    eq(out, "return\nvalue", "JavaScript restricted-production newline preservation");
    expect(out.find("return\n") != std::string::npos, "ASI-sensitive newline removed");
    expect(minify::javascript("a\n(b);a\n[b];a\n/regex/.test(b);", out, err), err);
    expect(out.find("a\n(b)") != std::string::npos &&
           out.find("a\n[b]") != std::string::npos &&
           out.find("a\n/regex/") != std::string::npos,
           "JavaScript expression-continuation newlines stripped");
    expect(minify::javascript("if(a){\nwork();\n}\nnext();", out, err), err);
    eq(out, "if(a){work()}\nnext()", "JavaScript closing-brace newline preservation");
    expect(minify::javascript("function f(){return call();}const x=1;", out, err), err);
    eq(out, "function f(){return call()}const x=1", "JavaScript redundant semicolon removal");
    expect(minify::javascript("function f(){while(test);label:;}for(;;);", out, err), err);
    eq(out, "function f(){while(test);label:;}for(;;);",
       "JavaScript meaningful empty statements preserved");
    expect(minify::javascript("if(test)function f(){}else;", out, err), err);
    eq(out, "if(test)function f(){}else;",
       "JavaScript Annex B empty else statement preserved");
    expect(minify::javascript("const a=true,b=false;return true;", out, err), err);
    eq(out, "const a=!0,b=!1;return!0", "JavaScript boolean literal shortening");
    expect(minify::javascript("const x={true:1,false(){return false}};x.true;", out, err), err);
    eq(out, "const x={true:1,false(){return!1}};x.true",
       "JavaScript boolean property names preserved");
    expect(minify::javascript("true.toString();false['valueOf']();new true;", out, err), err);
    eq(out, "true.toString();false['valueOf']();new true",
       "JavaScript boolean precedence boundaries preserved");
    expect(minify::javascript("const a=0xeac7,b=0b111111,c=0o17,d=0xffffffffffffffffn;", out, err), err);
    eq(out, "const a=60103,b=63,c=15,d=0xffffffffffffffffn",
       "JavaScript radix integer shortening");
    expect(minify::javascript("const a=\"don't stop\",b='say \\\'hi\\\'';", out, err), err);
    eq(out, "const a=\"don't stop\",b=\"say 'hi'\"",
       "JavaScript string quote selection");
    expect(minify::javascript("const a='\\n\\x41\\u0042\\\\';", out, err), err);
    eq(out, "const a='\\n\\x41\\u0042\\\\'",
       "JavaScript non-quote escapes preserved");
    expect(minify::javascript(
        "function total(longLeft,longRight){return longLeft+longRight;}", out, err), err);
    eq(out, "function total(longLeft,longRight){return longLeft+longRight}",
       "JavaScript conservative mode preserves parameter names");
    expect(minify::javascript(
        "function keep(longName){return {longName,value:longName}.longName;}", out, err), err);
    eq(out, "function keep(longName){return{longName,value:longName}.longName}",
       "JavaScript shorthand blocks parameter mangling");
    expect(minify::javascript("const x = value / *ptr; const y = left * /re/.test(s);", out, err), err);
    expect(out.find("/ *") != std::string::npos,
           "JS whitespace removal created a block-comment opener");
    expect(out.find("* /") != std::string::npos,
           "JS whitespace removal created a block-comment closer");


    // CSS: future at-rules should be treated as ordinary syntax rather than a
    // hard-coded allowlist, and calc() operator whitespace must remain valid.
    expect(minify::css("@media2 (width > 10px) { .x { color: red; } }", out, err), err);
    expect(out.find("@media2") != std::string::npos, "future CSS at-rule rejected or damaged");
    expect(minify::css(":root { --gap: calc(100% - 2rem); --blob: url(\"data:image/svg+xml,%3Csvg%20viewBox='0 0 1 1'%3E%3C/svg%3E\"); }", out, err), err);
    expect(out.find("calc(100% - 2rem)") != std::string::npos, "CSS calc operator whitespace damaged");
    expect(out.find("data:image/svg+xml") != std::string::npos, "CSS data URL damaged");
    expect(out.find("--gap:") != std::string::npos, "CSS custom property damaged");
    expect(minify::css("@container sidebar (width > 30rem) { .card { container-type: inline-size; } }", out, err), err);
    expect(out.find("@container") != std::string::npos && out.find("container-type:inline-size") != std::string::npos,
           "CSS container-query syntax damaged");
    expect(minify::css("@layer reset, base, theme; @layer theme { .x { color: color(display-p3 1 0 0 / .5); } }", out, err), err);
    expect(out.find("@layer") != std::string::npos && out.find("display-p3") != std::string::npos,
           "CSS layer/color syntax damaged");
    expect(minify::css("@supports selector(:has(*)) { .a:has(> .b) { width: clamp(1rem, 2vw + 1rem, 3rem); } }", out, err), err);
    expect(out.find("selector(:has(*))") != std::string::npos, "CSS :has()/supports syntax damaged");
    expect(out.find("2vw + 1rem") != std::string::npos, "CSS clamp/calc operator whitespace damaged");
    expect(minify::css(".a { & > .b { --tokens: {a:b}; margin-inline: 1cqi; } }", out, err), err);
    expect(out.find("&>.b") != std::string::npos || out.find("& > .b") != std::string::npos,
           "native CSS nesting damaged");
    expect(out.find("--tokens:") != std::string::npos, "CSS custom-property token stream damaged");

    expect(minify::jsx("const x=< mp<Map<string,number>> value={a} />;", out, err), err);
    expect(out.find("< mp") != std::string::npos,
           "JSX-mode whitespace removal manufactured a JSX opener");
    { std::string jsx_second; expect(minify::jsx(out, jsx_second, err), err); }

    // HTML: preserve whitespace text nodes rather than guessing display mode,
    // preserve raw-text comments literally, malformed input must fail cleanly.
    expect(minify::html("<span>a</span> <span>b</span><div> c </div>", out, err), err);
    expect(out.find("</span> <span>") != std::string::npos, "HTML inline inter-element space erased");
    expect(minify::html("<script><!-- not an html comment --></script><style>/* raw */ .x { a: b; }</style>", out, err), err);
    expect(out.find("<!-- not an html comment -->") != std::string::npos, "HTML raw script text changed");
    expect(out.find("/* raw */") != std::string::npos, "HTML raw style text changed");
    expect(!minify::html("<div class=\"x\"", out, err), "malformed HTML tag accepted");
    expect(err.find("unterminated HTML tag") != std::string::npos, "malformed HTML error missing");
    expect(minify::html("<link href=mailto:test@example.com\">", out, err), err);
    eq(out, "<link href=mailto:test@example.com\">", "HTML stray quote in unquoted attribute");
    expect(minify::html("<img crossorigin=anonymous />", out, err), err);
    eq(out, "<img crossorigin=anonymous />", "HTML unquoted attribute self-close boundary");
    expect(minify::html("<<script>  const x = 1;\n</script>", out, err), err);
    eq(out, "<<script>  const x = 1;\n</script>", "HTML recoverable raw-text opener");
    expect(minify::html("<div style=\"white-space: pre-line\">  a\n  b  </div><p>  c  </p>", out, err), err);
    eq(out, "<div style=\"white-space: pre-line\">  a\n  b  </div><p> c </p>",
       "HTML inline preserved-whitespace style");
    expect(minify::html("<area href=/ shape=default>", out, err), err);
    eq(out, "<area href=/ shape=default>", "HTML slash-ended unquoted attribute boundary");
    expect(minify::html("<listing>\n\nx</listing>", out, err), err);
    eq(out, "<listing>\n\nx</listing>", "HTML listing initial linefeed");
    expect(minify::html("<iframe> </body> </html> ", out, err), err);
    eq(out, "<iframe> </body> </html> ", "HTML iframe raw text");
    expect(minify::html("<svg><script> a <script> b </script> c </script></svg>", out, err), err);
    eq(out, "<svg><script> a <script> b </script> c </script></svg>", "HTML foreign SVG subtree");
    expect(minify::html("<p>x</p><!-- trailing", out, err), err);
    eq(out, "<p>x</p>", "HTML EOF comment recovery");
    expect(minify::javascript("''/*\u2028*/''", out, err), err);
    eq(out, "''\n''", "JavaScript Unicode line separator comment ASI");
    expect(minify::javascript("const x=`foo ${`bar ${5} baz`} qux`;", out, err), err);
    eq(out, "const x=`foo ${`bar ${5} baz`} qux`", "JavaScript nested template raw text");
    expect(minify::javascript("const x=/a/ instanceof RegExp;", out, err), err);
    eq(out, "const x=/a/ instanceof RegExp", "JavaScript regex keyword boundary");
    // Template-literal expressions must lex regular-expression literals so a
    // backtick, brace or `//`-lookalike inside a regex cannot corrupt template
    // or expression frame state. These were valid programs that Minify++
    // rejected as unterminated templates.
    expect(minify::javascript("const x=`a${/[`]/.test(s)}b`;", out, err), err);
    eq(out, "const x=`a${/[`]/.test(s)}b`", "JavaScript template regex backtick class");
    expect(minify::javascript("const x=`a${/[{}\\/]/.test(s)}b`;", out, err), err);
    eq(out, "const x=`a${/[{}\\/]/.test(s)}b`", "JavaScript template regex brace/escape class");
    expect(minify::javascript("const x=`a${s.replace(/\\//g, \"\")}b`;", out, err), err);
    eq(out, "const x=`a${s.replace(/\\//g, \"\")}b`", "JavaScript template regex escaped slash");
    expect(minify::javascript("const x=`a${1+/a{2}/.test(s)}b`;", out, err), err);
    eq(out, "const x=`a${1+/a{2}/.test(s)}b`", "JavaScript template regex after operator");
    expect(minify::javascript("const x=`a${`b${c}`}d`;", out, err), err);
    eq(out, "const x=`a${`b${c}`}d`", "JavaScript template nested template expression");
    // The same template/regular-expression lexing applies inside JSX
    // expression braces, where a backtick inside a regex character class used
    // to close the quoted-region scan early and corrupt the brace balance.
    expect(minify::jsx("const x = <A>{`v${/[`]/.test(s)}w`}</A>;", out, err), err);
    expect(out.find("`v${/[`]/.test(s)}w`") != std::string::npos,
           "JSX template expression regex backtick class damaged");
    expect(minify::jsx("const x = <A>{`a${`b${c}d`}e`}</A>;", out, err), err);
    expect(out.find("`a${`b${c}d`}e`") != std::string::npos,
           "JSX template expression nested template damaged");
    expect(minify::html("<p>héllo 😀 世界</p>", out, err), err);
    expect(out.find("héllo 😀 世界") != std::string::npos, "HTML Unicode damaged");
    expect(minify::html("<!doctype html><template><span>A</span> <span>B</span></template>", out, err), err);
    expect(out.find("<!doctype html>") != std::string::npos, "HTML doctype damaged");
    expect(out.find("</span> <span>") != std::string::npos, "HTML template mixed-content whitespace damaged");
    expect(minify::html("<textarea>  alpha\n  beta &amp; gamma </textarea>", out, err), err);
    expect(out.find("<textarea>  alpha\n  beta &amp; gamma </textarea>") != std::string::npos,
           "HTML textarea raw text damaged");
    expect(minify::html("<script type=\"module\">const s='<!--'; const t='-->'; </script>", out, err), err);
    expect(out.find("const s='<!--'; const t='-->';") != std::string::npos,
           "HTML module-script raw text damaged");
    expect(minify::html("< !-- not-a-comment -->", out, err), err);
    expect(out.find("< !--") != std::string::npos,
           "HTML whitespace removal manufactured comment syntax");

    expect(minify::xml("< ![CDATA[a < b]]_>", out, err), err);
    expect(out.find("< ![CDATA[") != std::string::npos,
           "XML whitespace removal manufactured CDATA syntax");

    // JS/TS/JSX: retain semicolons (while(cond); is an empty loop body), ASI
    // newlines, regex syntax, nested template expressions and JSX text.
    expect(minify::javascript("while (condition) ;\nnext();", out, err), err);
    expect(out.find("while(condition);") != std::string::npos, "empty while-loop statement semicolon stripped");
    expect(minify::javascript("const a=/[/]/g; const b=/a\\/\\/b/; const c=/[/*]/;", out, err), err);
    expect(out.find("/[/]/g") != std::string::npos, "regex character class damaged");
    expect(out.find("/a\\/\\/b/") != std::string::npos, "regex containing // damaged");
    expect(minify::javascript("const t=`hello ${name} ${`nested ${value}`}`;\n", out, err), err);
    expect(out.find("`hello ${name} ${`nested ${value}`}`") != std::string::npos, "nested template literal damaged");
    expect(minify::javascript("const el = <div className=\"x\">hello world {name}</div>;\n", out, err), err);
    expect(out.find("hello world") != std::string::npos, "JSX text whitespace damaged");
    expect(minify::javascript("type User = { name: string }; const x: User = { name: 'A' };\n", out, err), err);
    // Regex literals can legally appear after control-flow ')' where a naive
    // expression-context scanner often mistakes // inside the regex for a comment.
    expect(minify::javascript("if (ok) /https?:\\/\\//.test(url);\n", out, err), err);
    expect(out.find("/https?:\\/\\//") != std::string::npos, "regex after control-flow condition damaged");
    expect(minify::javascript("while (ok) /a\\/\\/b/.test(s);\n", out, err), err);
    expect(out.find("/a\\/\\/b/") != std::string::npos, "regex after while condition damaged");

    // Division followed by a regex operand is another ambiguity boundary.
    expect(minify::javascript("const z = value / /a\\/\\/b/.test(s);\n", out, err), err);
    expect(out.find("/a\\/\\/b/") != std::string::npos, "division/regex boundary damaged");

    expect(minify::javascript("const π = 3.14; const 世界 = 'ok';\n", out, err), err);
    expect(out.find("π") != std::string::npos && out.find("世界") != std::string::npos, "JS Unicode damaged");
    expect(!minify::javascript("const r = /unterminated\nx();", out, err), "unterminated JS regex accepted");

    // Idempotence: a second pass must be byte-for-byte stable for each format.
    std::string once, twice;
    expect(minify::json(" { \"x\" : [1, 2], \"s\":\"a b\" } ", once, err), err);
    expect(minify::json(once, twice, err), err); eq(twice, once, "json idempotence");
    expect(minify::css("/*!x*/ .a { width: calc(100% - 2rem); color: red; }", once, err), err);
    expect(minify::css(once, twice, err), err); eq(twice, once, "css idempotence");
    expect(minify::html("<div> a <span>b</span> c </div>", once, err), err);
    expect(minify::html(once, twice, err), err); eq(twice, once, "html idempotence");
    expect(minify::javascript("while(x); // keep semantics\nconst r=/a\\/b/;\n", once, err), err);
    expect(minify::javascript(once, twice, err), err); eq(twice, once, "js idempotence");
    expect(minify::jsx("const x=<span>https://example.com a  b</span>;\n", once, err), err);
    expect(minify::jsx(once, twice, err), err); eq(twice, once, "jsx idempotence");


    // Explicit JSX entry point is independently addressable by extension.
    expect(minify::jsx("const x = <><span>a b</span><span>{value}</span></>;\n", out, err), err);
    expect(out.find("</>") != std::string::npos, "JSX fragment damaged");
    expect(minify::jsx("const x=<p>https://example.com/a // literal text</p>;\n", out, err), err);
    expect(out.find("https://example.com/a // literal text") != std::string::npos,
           "JSX text was mistaken for a JavaScript comment");
    expect(minify::jsx(R"JSX(const el = <Widget className="card" data-id={value} aria-label="say \"hello\"">text</Widget>;)JSX", out, err), err);
    expect(out.find(R"JSX(<Widget className="card" data-id={value} aria-label="say \"hello\"">)JSX") != std::string::npos,
           "JSX tag or escaped attribute spelling damaged");

    const std::string adversarial_jsx =
        "cons//.test(s)}t x=Comp<Map<strin,number>> value={<a?.b ?? /[<>]a?.b ?? /[<>]//.test(s)} />;";
    if (minify::jsx(adversarial_jsx, once, err)) {
        expect(minify::jsx(once, twice, err),
               "successful adversarial JSX output rejected on second pass: " + err +
               "\nfirst output: " + once);
        eq(twice, once, "adversarial JSX idempotence");
    }

    // Malformed lexical constructs that are not browser-recoverable should fail
    // cleanly rather than silently emitting a half-minified program. CSS EOF
    // recovery is intentionally accepted below because browsers do the same.
    expect(!minify::javascript("const x = 'unterminated", out, err), "unterminated JS string accepted");
    expect(!minify::javascript("const x = `unterminated", out, err), "unterminated JS template accepted");
    expect(minify::css("a{/* unterminated", out, err), err);
    eq(out, "a{", "CSS EOF comment recovery");
    expect(minify::css("a{content:\"unterminated}", out, err), err);
    eq(out, "a{content:\"unterminated}", "CSS EOF string recovery");
    expect(!minify::html("<div data-x=\"unterminated>", out, err),
           "unterminated HTML attribute accepted");
    expect(!minify::xml("<node data-x=\"unterminated>", out, err),
           "unterminated XML attribute accepted");
    expect(!minify::svg("<svg data-x=\"unterminated>", out, err),
           "unterminated SVG attribute accepted");
    expect(!minify::jsx("const x=<Thing label=\"unterminated>;", out, err),
           "unterminated JSX attribute accepted");

    // More difficult template literal: nested template followed by raw text that
    // resembles a JS comment must remain template text.
    expect(minify::javascript("const x = `head ${`inner ${v}`} raw // still text`;\n", out, err), err);
    expect(out.find("raw // still text") != std::string::npos, "template raw text after nested template damaged");

    // Unquoted data URLs and custom syntax are preserved conservatively.
    expect(minify::css(R"CSS(.x { width: calc(100% - 2rem); --custom: 1  2; background:url("data:image/svg+xml,<svg><!--x--></svg>"); })CSS", out, err), err);
    expect(out.find("data:image/svg+xml") != std::string::npos, "unquoted CSS data URL damaged");


    // JavaScript lexical/ASI edge cases.
    expect(minify::javascript("try{}catch{} /https?:\\/\\//.test(s);", out, err), err);
    expect(out.find("/https?:\\/\\//") != std::string::npos,
           "regex after catch-without-binding block damaged");
    expect(minify::javascript("if(false){} /https?:\\/\\//.test(s);", out, err), err);
    expect(out.find("/https?:\\/\\//") != std::string::npos,
           "regex statement after block damaged");
    expect(minify::javascript("const x=function(){} / 2;", out, err), err);
    expect(out.find("}/2") != std::string::npos || out.find("} /2") != std::string::npos,
           "function-expression division mistaken for regex");
    expect(minify::javascript("const x=async function(){} / 2;", out, err), err);
    expect(out.find("}/2") != std::string::npos || out.find("} /2") != std::string::npos,
           "async function-expression division mistaken for regex");
    expect(minify::javascript("const x=class {static valueOf(){return 12}} / 2;", out, err), err);
    expect(out.find("}/2") != std::string::npos || out.find("} /2") != std::string::npos,
           "class-expression division mistaken for regex");
    expect(minify::javascript("const x=class X {static valueOf(){return 12}} / 2;", out, err), err);
    expect(out.find("}/2") != std::string::npos || out.find("} /2") != std::string::npos,
           "named class-expression division mistaken for regex");
    expect(minify::javascript("class C{} /https?:\\/\\//.test(s);", out, err), err);
    expect(out.find("/https?:\\/\\//") != std::string::npos,
           "regex after class declaration damaged");
    expect(minify::javascript("class C{} /[/*}]/.test(s);", out, err), err);
    expect(out.find("/[/*}]/") != std::string::npos,
           "regex character class after class declaration mistaken for comment");
    expect(minify::javascript("label:{} /https?:\\/\\//.test(s);", out, err), err);
    expect(out.find("/https?:\\/\\//") != std::string::npos,
           "regex after labelled block damaged");
    expect(minify::javascript("const x=true?1:{valueOf(){return 12}} / 2;", out, err), err);
    expect(out.find("}/2") != std::string::npos || out.find("} /2") != std::string::npos,
           "ternary object division mistaken for labelled block regex");
    expect(minify::javascript("function f(){} /a/.test(s);", out, err), err);
    expect(out.find("/a/.test") != std::string::npos, "regex after function declaration damaged");
    expect(minify::javascript("async function f(){for await(const x of xs) /a/.test(x);}", out, err), err);
    expect(out.find("/a/.test") != std::string::npos, "regex after for-await control parenthesis damaged");
    expect(minify::javascript("const n={valueOf(){return 12}} / 2;", out, err), err);
    expect(out.find("}/2") != std::string::npos || out.find("} /2") != std::string::npos ||
           out.find("}/ 2") != std::string::npos,
           "object-literal division mistaken for regex after brace");
    expect(minify::javascript("const n=({valueOf(){return 12}}) / 2 / d;", out, err), err);
    expect(out.find("/2/d") != std::string::npos || out.find("/2 /d") != std::string::npos ||
           out.find("/ 2 / d") != std::string::npos,
           "object-expression division damaged by regex-after-brace handling");
    expect(minify::javascript("while (condition);", out, err), err);
    expect(out.find(';') != std::string::npos, "empty while statement semicolon stripped");
    expect(minify::javascript("if (x) { while (y); }", out, err), err);
    expect(out.find("while") != std::string::npos && out.find(';') != std::string::npos,
           "nested empty while statement damaged");
    expect(minify::javascript("const a=/[/]/; const b=/a\\/b/g; const c=/[/*]/;", out, err), err);
    expect(out.find("/[/]/") != std::string::npos, "regex slash class damaged");
    expect(out.find("/a\\/b/g") != std::string::npos, "escaped regex slash damaged");
    expect(minify::javascript("const π = 3; return π;", out, err), err);
    expect(out.find("const π") != std::string::npos && out.find("return π") != std::string::npos,
           "Unicode JS identifier boundary collapsed");
    expect(minify::javascript("const 你好 = 1;", out, err), err);
    expect(out.find("const 你好") != std::string::npos, "CJK JS identifier boundary collapsed");
    expect(minify::css(".café { --颜色: red; }", out, err), err);
    expect(out.find(".café") != std::string::npos && out.find("--颜色") != std::string::npos,
           "Unicode CSS identifier damaged");
    expect(minify::javascript("const s = 1 .toString();", out, err), err);
    expect(out.find("1 .toString") != std::string::npos, "numeric literal/member boundary collapsed");
    expect(minify::javascript("const s = 0x1 .toString();", out, err), err);
    expect(out.find("1 .toString") != std::string::npos, "hex numeric/member boundary collapsed");
    expect(minify::javascript("const s = 1e3 .toString();", out, err), err);
    expect(out.find("1e3 .toString") != std::string::npos, "exponent numeric/member boundary collapsed");
    expect(minify::javascript("const y = x / /a/.test(s);", out, err), err);
    expect(out.find("x//") == std::string::npos, "division followed by regex collapsed into line comment");
    expect(out.find("/a/.test") != std::string::npos, "division-followed regex damaged");
    expect(minify::javascript("const y = x / /[/*]/.test(s);", out, err), err);
    expect(out.find("/[/*]/.test") != std::string::npos, "regex character class after division damaged");
    expect(minify::javascript("const x = a / b / c;", out, err), err);
    expect(out.find("a / b / c") != std::string::npos || out.find("a/b/c") != std::string::npos,
           "division expression mistaken for regex");
    expect(minify::javascript("const t=`hello ${name} // not comment ${1+2}`;", out, err), err);
    expect(out.find("// not comment") != std::string::npos, "template literal contents damaged");
    expect(minify::javascript("const t=`outer ${`inner ${x}`}`;", out, err), err);
    expect(out.find("`outer ${`inner ${x}`}`") != std::string::npos, "nested template damaged");
    expect(minify::javascript("function f(){return\n{x:1};}", out, err), err);
    expect(out.find("return\n") != std::string::npos, "return ASI newline damaged");
    expect(minify::javascript("a\n++b;", out, err), err);
    expect(out.find("\n++") != std::string::npos, "prefix increment newline damaged");

    // TS/JSX are deliberately accepted by the conservative JS-family pass.
    expect(minify::javascript("interface User { name: string }\\nconst x: number = 1;", out, err), err);
    expect(minify::javascript(R"JS(const el = <Button title="a  b">{name}</Button>;)JS", out, err), err);
    expect(out.find("<Button") != std::string::npos && out.find("</Button>") != std::string::npos,
           "JSX syntax damaged");

    // CSS modern/future syntax: unknown at-rules/functions/properties are opaque tokens,
    // not a whitelist. Whitespace-sensitive values and strings must survive.
    expect(minify::css("@media2 (width > 10px) { .x { future-prop: future-fn(1, 2); } }", out, err), err);
    expect(out.find("@media2") != std::string::npos && out.find("future-fn") != std::string::npos,
           "unknown future CSS syntax rejected");
    expect(minify::css(R"CSS(.x { width: calc(100% - 2rem); --custom: 1  2; background:url("data:image/svg+xml,<svg><!--x--></svg>"); })CSS", out, err), err);
    expect(out.find("calc(") != std::string::npos, "calc damaged");
    expect(out.find("--custom") != std::string::npos, "custom property damaged");
    expect(out.find("data:image/svg+xml") != std::string::npos, "data URL damaged");
    expect(minify::css("@supports selector(:has(*)) { @container card (width > 20rem) { .x { color: oklch(60% .2 20); } } }", out, err), err);
    expect(out.find("@container") != std::string::npos && out.find("oklch") != std::string::npos,
           "modern CSS syntax damaged");

    // HTML whitespace/raw text/comments/Unicode.
    expect(minify::html("a<!--x-->b", out, err), err);
    eq(out, "ab", "HTML comment inserted text whitespace");
    expect(minify::html("a <!--x--> b", out, err), err);
    eq(out, "a b", "HTML comment surrounding whitespace changed");
    expect(minify::html("<div>a</div><!--x--><div>b</div>", out, err), err);
    eq(out, "<div>a</div><div>b</div>", "HTML comment inserted element whitespace");
    expect(minify::html("<span>A</span> <span>B</span>", out, err), err);
    eq(out, "<span>A</span> <span>B</span>", "inline whitespace");
    expect(minify::html("<div>A</div>\n<div>B</div>", out, err), err);
    expect(out.find("</div> <div>") != std::string::npos, "inter-element whitespace erased");
    expect(minify::html("<script>const s=\"</scriptx>\";   const x = 1;</script>", out, err), err);
    expect(out.find("</scriptx>\";   const x = 1;") != std::string::npos,
           "HTML raw script closed on a tag-name prefix");
    expect(minify::html("<pre>a</prex>   b</pre>", out, err), err);
    expect(out.find("a</prex>   b") != std::string::npos,
           "HTML preformatted block closed on a tag-name prefix");
    expect(minify::html("<script>/* keep */ const x='<!-- keep -->';</script><style>/* keep */ .x { }</style>", out, err), err);
    expect(out.find("/* keep */") != std::string::npos && out.find("<!-- keep -->") != std::string::npos,
           "raw-text comment contents changed");
    expect(minify::html("<p>你好   😀   café</p>", out, err), err);
    expect(out.find("你好 😀 café") != std::string::npos, "Unicode HTML text damaged");

    // Non-recoverable malformed inputs fail rather than emitting guessed output.
    // CSS comments are recoverable at EOF according to CSS Syntax.
    expect(!minify::html("<div", out, err), "unterminated HTML tag accepted");
    expect(minify::css("a{/*", out, err), err);
    eq(out, "a{", "CSS short EOF comment recovery");
    expect(!minify::javascript("/*", out, err), "unterminated JS comment accepted");

    // The separator oracle must prevent adjacent source tokens from being
    // reinterpreted as identifiers, comments, update operators or members.
    expect(minify::javascript("a + +b; c - -d; 1 .toString(); /x/g instanceof RegExp;", out, err), err);
    expect(out.find("a+ +b") != std::string::npos, "plus tokens merged");
    expect(out.find("c - -d") != std::string::npos, "minus tokens merged");
    expect(out.find("1 .toString") != std::string::npos, "numeric member boundary merged");
    expect(out.find("/x/g instanceof") != std::string::npos, "regex flag boundary merged");

    expect(minify::javascript("function f(){return\nvalue}\na\n++b\nasync\nx=>x", out, err), err);
    expect(out.find("return\nvalue") != std::string::npos, "return line terminator removed");
    expect(out.find("a\n++b") != std::string::npos, "postfix line terminator removed");
    expect(out.find("async\nx=>x") != std::string::npos, "async arrow boundary removed");

    expect(minify::javascript("while(condition);", out, err), err);
    eq(out, "while(condition);", "while empty statement removed");
    expect(minify::javascript("for(;;);", out, err), err);
    eq(out, "for(;;);", "for empty statement removed");
    expect(minify::javascript("label:;", out, err), err);
    eq(out, "label:;", "label empty statement removed");
    expect(minify::javascript("do;while(condition);", out, err), err);
    eq(out, "do;while(condition);", "do/while empty statement removed");

    // Idempotence: a second minification pass must be byte-identical.
    auto idem = [&](minify::Format fmt, const std::string& src, const char* label) {
        std::string a,b,e;
        expect(minify::run(fmt,src,a,e), e);
        expect(minify::run(fmt,a,b,e), e);
        if (a != b) {
            std::cerr << label << " is not idempotent\\nfirst: [" << a << "]\\nsecond:[" << b << "]\\n";
            std::exit(1);
        }
    };
    idem(minify::Format::Json, R"({"x":[1, 2],"s":"a b"})", "JSON");
    idem(minify::Format::Html, "<div>  A <span>B</span> C </div>", "HTML");
    idem(minify::Format::Css, "/*!x*/ .a { width: calc(100% - 1rem); }", "CSS");
    idem(minify::Format::JavaScript, "const r=/a\\\\/b/g; //x\nwhile(ok);\n", "JavaScript");


    {
        const std::string jsx_src = "const  el = <div>https://example.com/{ name + 1 }</div>;";
        expect(minify::jsx(jsx_src, out, err), err);
        expect(out.find("https://example.com/") != std::string::npos, "JSX URL text damaged");
        expect(out.find("{name+1}") != std::string::npos, "JSX expression was not minified");
    }
    auto idem_fn = [&](auto fn, const std::string& src, const char* label) {
        std::string a,b,e;
        expect(fn(src,a,e), e);
        expect(fn(a,b,e), e);
        if (a != b) {
            std::cerr << label << " is not idempotent\n";
            std::exit(1);
        }
    };
    idem_fn([](const std::string& source, std::string& result, std::string& message) {
        return minify::jsx(source, result, message);
    }, "const x=<div> hello  world </div>;", "JSX");


    // JSX: preserve text/markup spelling, but minify embedded JS expressions and
    // ordinary JS surrounding the JSX region.
    expect(minify::jsx("const  el = <div className=\"x\"> hello  world {  value +  1  } </div> ;", out, err), err);
    expect(out.find("const el=") != std::string::npos, "JS around JSX was not minified");
    expect(out.find(" hello  world ") != std::string::npos, "JSX text whitespace changed");
    expect(out.find("{value+1}") != std::string::npos, "JSX expression was not minified");
    expect(minify::jsx("const x = <div>{ true ? 0xff : 'value' }</div>;", out, err), err);
    eq(out, "const x=<div>{true?0xff:'value'}</div>;",
       "JSX preserves non-trivia JavaScript tokens");
    expect(minify::jsx("const x=<><span>A</span><span>{ b + 1 }</span></>;", out, err), err);
    expect(out.find("{b+1}") != std::string::npos, "fragment JSX expression damaged");
    expect(!minify::jsx("const x=<div>{a+1</div>;", out, err), "unterminated JSX expression accepted");

    expect(minify::xml("<?target  a   b?><root/>", out, err), err);
    expect(out.find("<?target  a   b?>") != std::string::npos,
           "XML processing-instruction data whitespace changed");
    expect(!minify::xml("<?target missing", out, err),
           "unterminated XML processing instruction accepted");

    // XML: comments/formatting between tags may disappear; text and CDATA remain.
    expect(minify::xml("<?xml version=\"1.0\"?>\n<root>\n  <a x=\"1  2\"> text  stays </a><!--x-->\n  <b><![CDATA[ a < b ]]></b>\n</root>", out, err), err);
    expect(out.find("<!--x-->") == std::string::npos, "XML comment retained");
    expect(out.find(" text  stays ") != std::string::npos, "XML text whitespace changed");
    expect(out.find("<![CDATA[ a < b ]]>") != std::string::npos, "XML CDATA changed");
    expect(!minify::xml("<root", out, err), "unterminated XML tag accepted");
    expect(!minify::xml("<root><!--", out, err), "unterminated XML comment accepted");

    // SVG shares XML's conservative text rules: visible <text>/<tspan> content
    // must not be collapsed merely to save bytes.
    expect(minify::xml("<p><b>A</b> <i>B</i></p>", out, err), err);
    expect(out.find("</b> <i>") != std::string::npos, "XML mixed-content whitespace removed");
    expect(minify::svg("<text><tspan>A</tspan> <tspan>B</tspan></text>", out, err), err);
    expect(out.find("</tspan> <tspan>") != std::string::npos, "SVG text-node whitespace removed");
    expect(minify::xml("<root xmlns:x=\"urn:x\"><x:item a=\"1 &amp; 2\">A &lt; B</x:item></root>", out, err), err);
    expect(out.find("xmlns:x=\"urn:x\"") != std::string::npos && out.find("A &lt; B") != std::string::npos,
           "XML namespace/entity content damaged");
    expect(minify::xml("<root><![CDATA[ x ]]> text <![CDATA[ y < z ]]></root>", out, err), err);
    expect(out.find("<![CDATA[ x ]]> text <![CDATA[ y < z ]]>") != std::string::npos,
           "XML adjacent CDATA/mixed text damaged");
    expect(minify::svg("<svg viewBox=\"0 0 10 10\"><path d=\"M 0 0 L 10 10 Z\"/><text xml:space=\"preserve\"> A  B </text></svg>", out, err), err);
    expect(out.find("d=\"M 0 0 L 10 10 Z\"") != std::string::npos, "SVG path attribute damaged");
    expect(out.find(" A  B ") != std::string::npos, "SVG xml:space text damaged");
    expect(minify::jsx("const n=a<b&&c>d;", out, err), err);
    expect(out.find("a<b&&c>d") != std::string::npos, "compact JS comparison mistaken for JSX");
    expect(minify::jsx("const x=foo<Bar>(baz);", out, err), err);
    expect(out.find("foo<Bar>(baz)") != std::string::npos, "generic-looking JS mistaken for JSX");
    expect(minify::jsx("function f(){return <Thing/>;}", out, err), err);
    expect(out.find("return<Thing/>") != std::string::npos, "JSX after return was not recognized");
    expect(minify::jsx("const x=<div>{ /}/.test(s) }</div>;", out, err), err);
    expect(out.find("{/}/.test(s)}") != std::string::npos,
           "regex brace prematurely closed JSX child expression");
    expect(minify::jsx("const x=<div>{cond ? <span>https://example.com/x</span> : null}</div>;", out, err), err);
    expect(out.find("https://example.com/x") != std::string::npos, "nested JSX text in child expression damaged");
    expect(minify::jsx("const x=<Comp child={<span>{ value + 1 }</span>} />;", out, err), err);
    expect(out.find("{value+1}") != std::string::npos, "nested JSX attribute child expression not minified recursively");
    expect(minify::jsx("const x=<div>{ a /* } */ + b }</div>;", out, err), err);
    expect(out.find("{a+b}") != std::string::npos,
           "comment brace prematurely closed JSX child expression");
    expect(minify::jsx("const x=<Thing value={ /}/.test(s) }/>;", out, err), err);
    expect(out.find("value={/}/.test(s)}") != std::string::npos,
           "regex brace prematurely closed JSX attribute expression");
    expect(minify::jsx("const x = <Thing value={a > b ? x : y} />;", out, err), err);
    expect(out.find("{a>b?x:y}") != std::string::npos, "JSX attribute comparison terminated tag early");
    expect(minify::jsx("const x = <Thing value={{limit: a > b ? 2 : 1}} />;", out, err), err);
    expect(out.find("limit:a>b?2:1") != std::string::npos, "nested JSX attribute object expression damaged");
    expect(minify::jsx("const x = <Thing value={`x > ${a}`} />;", out, err), err);
    expect(out.find("`x > ${a}`") != std::string::npos, "template literal in JSX attribute damaged");
    expect(minify::jsx("const x = <Thing value={ a + 1 } />;", out, err), err);
    expect(out.find("<Thing") != std::string::npos && out.find("{a+1}") != std::string::npos,
           "self-closing JSX root damaged");
    expect(minify::jsx("const x = <><A/><B>{ {x: 1}.x }</B></>;", out, err), err);
    expect(out.find("{ {x:1}.x}") != std::string::npos || out.find("{{x:1}.x}") != std::string::npos,
           "nested JSX object expression damaged");
    // TypeScript JSX-conformance regressions: separate JavaScript fragments
    // around a JSX root must retain ASI boundaries, closing tags are never
    // fresh roots, and a line comment before a standalone root is trivia.
    expect(minify::jsx("import {h} from './h'\n<h></h>\nexport * from './x';", out, err), err);
    expect(out.find("'./h'\n<h>") != std::string::npos,
           "line boundary before standalone JSX root removed");
    expect(out.find("</h>\nexport") != std::string::npos,
           "line boundary after JSX root removed");
    expect(minify::jsx("function Test() {}\n<Test></Test>\n", out, err), err);
    expect(out.find("<Test></Test>") != std::string::npos,
           "closing JSX tag was treated as a fresh root");
    expect(minify::jsx("// component expression\n<M a={() => <button>test</button>}/>\n"
                       "class Next {}", out, err), err);
    expect(out.find("<button>test</button>") != std::string::npos &&
           out.find("/>\nclass") != std::string::npos,
           "comment-delimited JSX root or following ASI boundary damaged");
    expect(minify::svg("<svg xmlns=\"http://www.w3.org/2000/svg\">\n <text>hello   world</text>\n <path d=\"M 0 0 L 10 10\" />\n</svg>", out, err), err);
    expect(out.find("hello   world") != std::string::npos, "SVG text whitespace changed");
    expect(out.find("M 0 0 L 10 10") != std::string::npos, "SVG path attribute changed");

    idem(minify::Format::Xml, "<root>\\n <a> text  here </a>\\n</root>", "XML");
    idem(minify::Format::Svg, "<svg>\\n<text>a  b</text>\\n</svg>", "SVG");
    idem(minify::Format::Jsx, "const x = <div>{ value + 1 }</div>;", "JSX");
    expect(minify::jsx(
        "cost x=<Comp<Map<string,numbZr>> value={a?.b ?? /[<>]//.est(s)} />.est(s)} />",
        out, err), err);
    std::string recovered;
    expect(minify::jsx(out, recovered, err), "JSX regex/division recovery: " + err);

    minify::Format f;
    expect(minify::format_for_extension(".html", f) && f == minify::Format::Html, "html extension");
    expect(minify::format_for_extension("MJS", f) && f == minify::Format::JavaScript, "mjs extension");
    expect(minify::format_for_extension(".jsx", f) && f == minify::Format::Jsx, "jsx extension");
    expect(minify::format_for_extension(".xml", f) && f == minify::Format::Xml, "xml extension");
    expect(minify::format_for_extension(".svg", f) && f == minify::Format::Svg, "svg extension");
    expect(!minify::format_for_extension(".ts", f), "TypeScript source extension unexpectedly supported");
    expect(!minify::format_for_extension(".tsx", f), "TSX source extension unexpectedly supported");

    const std::string policy_source =
        "const value = {number: 0xff, text: 'x', match: /x+/giu, "
        "template: `raw ${value + `${/}/.test(text) ? {x: 1}.x : 0}`}`};";
    std::string conservative, structured, aggressive;
    expect(minify::javascript(policy_source, conservative, err,
                              {minify::OptimizationLevel::Conservative}), err);
    expect(minify::javascript(policy_source, structured, err,
                              {minify::OptimizationLevel::Structured}), err);
    expect(minify::javascript(policy_source, aggressive, err,
                              {minify::OptimizationLevel::Aggressive}), err);
    eq(structured, conservative, "inactive structured policy changed output");
    eq(aggressive, conservative, "inactive aggressive policy changed output");

    const std::string jsx_policy_source =
        "const  view = <Panel value={ left +  right }>"
        "  exact  text <Child>{/*gone*/ nested + 1}</Child></Panel>;";
    expect(minify::jsx(jsx_policy_source, conservative, err,
                       {minify::OptimizationLevel::Conservative}), err);
    expect(minify::jsx(jsx_policy_source, structured, err,
                       {minify::OptimizationLevel::Structured}), err);
    expect(minify::jsx(jsx_policy_source, aggressive, err,
                       {minify::OptimizationLevel::Aggressive}), err);
    eq(structured, conservative, "inactive structured JSX policy changed output");
    eq(aggressive, conservative, "inactive aggressive JSX policy changed output");
    expect(conservative.find("  exact  text ") != std::string::npos,
           "JSX child text changed across policy regions");
    expect(conservative.find("value={left+right}") != std::string::npos &&
           conservative.find("{nested+1}") != std::string::npos,
           "JSX JavaScript regions were not conservatively minified");

    expect(minify::javascript("function total(longLeft,longRight){return longLeft+longRight;}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function total($,_){return $+_}","structured simple parameter renaming");
    expect(minify::javascript("function total(longValue){const doubledValue=longValue*2;return doubledValue;}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function total($){const _=$*2;return _}","structured local binding renaming");
    expect(minify::javascript("function total(longValue=1,otherValue=2){let firstValue=longValue,secondValue=otherValue;return firstValue+secondValue;}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function total($=1,_=2){let a=$,b=_;return a+b}","structured simple default and comma binding discovery");
    expect(minify::javascript("function total(){var repeatedValue=1;var repeatedValue;return repeatedValue;}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function total(){var $=1;var $;return $}","structured merged var declaration identity");
    expect(minify::javascript("function keep(longName){return {longName};}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function keep($){return{longName:$}}","structured shorthand-aware printing");
    expect(minify::javascript("function keep(longName){return {...longName};}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function keep($){return{...$}}","structured object spread reference renaming");
    expect(minify::javascript("const total=(longValue)=>{const doubledValue=longValue*2;return doubledValue;};",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const total=$=>{const _=$*2;return _}","structured block arrow scope safety");
    expect(minify::javascript("const total=(longValue)=>longValue*2;",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const total=$=>$*2","structured concise arrow parameter renaming");
    expect(minify::javascript("const total=longValue=>({longValue,externalValue});",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const total=$=>({longValue:$,externalValue})","structured concise arrow shorthand preservation");
    expect(minify::javascript("const total=longValue=>()=>longValue;",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const total=$=>()=>$","structured concise arrow capture mangling");
    expect(minify::javascript("function choose(async){return async?left:right}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function choose($){return $?left:right}","structured contextual async reference mangling");
    expect(minify::javascript("const box={method(longParameter){const localValue=longParameter;return localValue}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const box={method($){const _=$;return _}}","structured object method scope mangling");
    expect(minify::javascript("class Box{method(longParameter){return longParameter}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"class Box{method($){return $}}","structured class method scope mangling");
    expect(minify::javascript("const box={async load(longName){return longName},*walk(otherName){yield otherName},set value(nextValue){this._value=nextValue}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const box={async load($){return $},*walk($){yield $},set value($){this._value=$}}","structured async generator and setter method mangling");
    expect(minify::javascript("function nested({firstName:nestedName,deep:{secondName},...remainingValues},[arrayValue]){return nestedName+secondName+remainingValues.length+arrayValue}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function nested({firstName:$,deep:{secondName:_},...a},[b]){return $+_+a.length+b}","structured recursive destructuring parameter mangling");
    expect(minify::javascript("function declarations(source){const {longProperty:localValue,nested:{deepValue},...restValues}=source;return localValue+deepValue+restValues.count}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function declarations($){const{longProperty:_,nested:{deepValue:a},...b}=$;return _+a+b.count}","structured recursive destructuring declaration mangling");
    expect(minify::javascript("function entries(items){for(const [longKey,longValue] of items){use(longKey,longValue)}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function entries($){for(const[_,a]of $){use(_,a)}}","structured for-of destructuring binding mangling");
    expect(minify::javascript("function keys(object){for(const {longKey} in object){use(longKey)}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function keys($){for(const{longKey:_}in $){use(_)}}","structured for-in destructuring binding mangling");
    expect(minify::javascript("function loops(items){for(let longIndex=0;longIndex<items.length;longIndex++){use(longIndex)}let longIndex=3;return longIndex}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function loops($){for(let longIndex=0;longIndex<$.length;longIndex++){use(longIndex)}let longIndex=3;return longIndex}","structured shadowed for-head fallback");
    expect(minify::javascript("function caught(){try{work()}catch({message:longMessage,code:{valueCode}}){return longMessage+valueCode}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function caught(){try{work()}catch({message:$,code:{valueCode:_}}){return $+_}}","structured recursive catch binding mangling");
    expect(minify::javascript("function outer(){function longHelper(longValue){return longValue}use(longHelper(1));return 2}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function outer(){function $($){return $}use($(1));return 2}","aggressive local function binding mangling");
    expect(minify::javascript("function outer(){const factorial=function longFactorial(value){return value?value*longFactorial(value-1):1};return factorial(4)}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function outer(){const $=function _($){return $?$*_($-1):1};return $(4)}","structured named function expression mangling");
    expect(minify::javascript("function make(){class LongClass{method(longValue){return longValue}static{var staticValue=1;use(staticValue)}}return new LongClass}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function make(){class ${method($){return $}static{var $=1;use($)}}return new $}","aggressive class binding with method and static-block mangling");
    expect(minify::javascript("function make(){class RecursiveClass{method(){return RecursiveClass}}return RecursiveClass}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function make(){class RecursiveClass{method(){return RecursiveClass}}return RecursiveClass}","structured self-referential class fallback");
    expect(minify::javascript("function outer(longValue){class Box{method(otherValue){return otherValue}}return longValue}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function outer($){class _{method($){return $}}return $}","structured class subtree does not block unrelated outer binding");
    expect(minify::javascript("function outer(longValue){class Box{field=longValue;method(otherValue){return otherValue}}return longValue}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function outer(longValue){class ${field=longValue;method($){return $}}return longValue}","structured opaque class reference blocks only affected outer binding");
    expect(minify::javascript("function keep(mangle_options){return {mangle_options(){return 1},value:mangle_options}}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function keep($){return{mangle_options(){return 1},value:$}}","method key excluded from binding mangling");
    expect(minify::javascript("function total(...longValues){return longValues.length;}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function total(...$){return $.length}","structured rest parameter binding");
    expect(minify::javascript("function total({longValue}){return longValue+longValue;}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function total({longValue:$}){return $+$}","structured object destructuring parameter");
    expect(minify::javascript("function total([longValue]){return longValue+longValue;}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function total([$]){return $+$}","structured array destructuring parameter");
    expect(minify::javascript("function total({longValue}){return function(){return longValue+longValue}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function total({longValue:$}){return function(){return $+$}}","structured captured destructuring parameter");
    expect(minify::javascript("function blocks(flag){if(flag){let firstValue=1;use(firstValue)}else{let secondValue=2;use(secondValue)}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function blocks($){if($){let _=1;use(_)}else{let _=2;use(_)}}","structured disjoint lexical name reuse");
    expect(minify::javascript("function siblings(){function first(longValue){return longValue}function second(otherValue){return otherValue}return first(1)+second(2)}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function siblings(){function $($){return $}function _($){return $}return $(1)+_(2)}","structured sibling function mangling and local name reuse");
    expect(minify::javascript("function sequential(){var firstValue=1;use(firstValue);var secondValue=2;use(secondValue)}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function sequential(){var $=1;use($);var $=2;use($)}","structured straight-line var live-range reuse");
    expect(minify::javascript("function keep(longName){return {value:call(0,longName,2)};}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function keep($){return{value:call(0,$,2)}}","structured call argument is not shorthand");
    expect(minify::javascript("function keep(longName){return eval('longName');}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function keep(longName){return eval('longName')}","structured dynamic lookup exclusion");
    expect(minify::javascript("function keep(longName){return (eval)('longName');}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function keep(longName){return(eval)('longName')}","structured parenthesized direct eval exclusion");
    expect(minify::javascript("function keep(longName){const localValue=1;return arguments[0]+localValue}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function keep(longName){const $=1;return arguments[0]+$}","structured arguments preserves parameters but permits independent locals");
    expect(minify::javascript("function keep(longName){return function(){return eval('longName')}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function keep(longName){return function(){return eval('longName')}}","structured descendant dynamic lookup barrier");
    expect(minify::javascript("function outer(longValue){return function(innerValue){return function(deepValue){return longValue+innerValue+deepValue}}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function outer($){return function(_){return function(a){return $+_+a}}}","structured transitive captured binding allocation");
    expect(minify::javascript("function outer(longValue){const callback=(innerValue)=>{return longValue+innerValue};return callback}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function outer($){const _=a=>{return $+a};return _}","structured coordinated block-arrow mangling");
    std::string original_signature, renamed_signature;
    expect(minify::javascript_binding_signature("function total(longValue){return longValue+externalValue}",original_signature,err),err);
    expect(minify::javascript_binding_signature("function total($){return $+externalValue}",renamed_signature,err),err);
    eq(renamed_signature,original_signature,"binding signature accepts consistent alpha renaming");
    expect(minify::javascript_binding_signature("function total($){return otherValue+externalValue}",renamed_signature,err),err);
    expect(renamed_signature != original_signature,"binding signature rejected inconsistent alpha renaming");
    expect(minify::javascript_binding_signature("function outer(){function longHelper(value){return longHelper(value-1)}return longHelper(2)}",original_signature,err),err);
    expect(minify::javascript_binding_signature("function outer(){function $(value){return $(value-1)}return $(2)}",renamed_signature,err),err);
    eq(renamed_signature,original_signature,"named function declaration binding topology");
    expect(minify::javascript_binding_signature("function outer(){return second(2);function first(value){return second(value)}function second(value){return value?first(value-1):0}}",original_signature,err),err);
    expect(minify::javascript_binding_signature("function outer(){return _(2);function $(value){return _(value)}function _(value){return value?$(value-1):0}}",renamed_signature,err),err);
    eq(renamed_signature,original_signature,"hoisted mutually recursive function topology");
    expect(minify::javascript_binding_signature("function outer(){function helper(){return 1}function nested(){function helper(){return 2}return helper()}return helper()+nested()}",original_signature,err),err);
    expect(minify::javascript_binding_signature("function outer(){function $(){return 1}function nested(){function _(){return 2}return _()}return $()+nested()}",renamed_signature,err),err);
    eq(renamed_signature,original_signature,"nested shadowed function declaration topology");
    const std::vector<std::pair<std::string,std::string>> function_boundary_cases = {
        {"function outer(){function helper(value=helper){return class Box{field=helper;method(){return helper(value)}}}return helper()}","function outer(){function $(value=$){return class Box{field=$;method(){return $(value)}}}return $()}"},
        {"function outer(){function helper(){return 1}class Box{static value=helper;static{use(helper)}method(){return helper()}}return Box}","function outer(){function $(){return 1}class Box{static value=$;static{use($)}method(){return $()}}return Box}"}
    };
    for (const auto& item : function_boundary_cases) {
        expect(minify::javascript_binding_signature(item.first,original_signature,err),err);
        expect(minify::javascript_binding_signature(item.second,renamed_signature,err),err);
        eq(renamed_signature,original_signature,"named function cross-boundary topology");
    }
    expect(minify::javascript_binding_signature("function outer(){function helper(){return 1}return {publicHelper:helper,method:helper}.publicHelper}",original_signature,err),err);
    expect(minify::javascript_binding_signature("function outer(){function $(){return 1}return {publicHelper:$,method:$}.publicHelper}",renamed_signature,err),err);
    eq(renamed_signature,original_signature,"function binding names remain distinct from observable property spelling");
    expect(minify::javascript_binding_signature("import {sourceName as localName} from 'pkg';export {localName as publicName};use(localName)",original_signature,err),err);
    expect(minify::javascript_binding_signature("import {sourceName as $} from 'pkg';export {$ as publicName};use($)",renamed_signature,err),err);
    eq(renamed_signature,original_signature,"module signature resolves local aliases while preserving external names");
    expect(minify::javascript_binding_signature("import {sourceName as $} from 'pkg';export {$ as changedName};use($)",renamed_signature,err),err);
    expect(renamed_signature != original_signature,"module signature keeps exported spelling observable");
    expect(minify::javascript_mangle_report("function total(longValue){return longValue+externalValue}",original_signature,err),err);
    expect(original_signature.find("bindings\t2")!=std::string::npos&&
           original_signature.find("eligible\t1")!=std::string::npos&&
           original_signature.find("top-level\t1")!=std::string::npos,
           "mangle coverage report");
    minify::Options top_level_options(minify::OptimizationLevel::Aggressive);
    top_level_options.mangle_top_level=true;
    expect(minify::javascript("const longValue=1;use(longValue)",aggressive,err,top_level_options),err);
    eq(aggressive,"const $=1;use($)","explicit closed-world top-level mangling");
    expect(minify::javascript("export const longValue=1;use(longValue)",aggressive,err,top_level_options),err);
    eq(aggressive,"export const longValue=1;use(longValue)","top-level mangling excludes modules");
    expect(minify::javascript_effect_signature("function total(longValue){return call(longValue)}",original_signature,err),err);
    expect(minify::javascript_effect_signature("function total($){return call($)}",renamed_signature,err),err);
    eq(renamed_signature,original_signature,"effect signature accepts alpha renaming");
    expect(minify::javascript_ir_signature("function f(a){return(a+1)}",original_signature,err),err);
    expect(minify::javascript_ir_signature("function  f ( a ) { return ( a + 1 ) }",renamed_signature,err),err);
    eq(renamed_signature,original_signature,"IR node IDs ignore trivia spelling");
    expect(minify::javascript_ir_signature("a=b?c+d:e*f,g",original_signature,err),err);
    expect(original_signature.find("E")!=std::string::npos,"IR expression inventory");
    expect(minify::javascript_ir_signature("new Box(obj.value,import('x'))",original_signature,err),err);
    expect(original_signature.find("A")!=std::string::npos&&original_signature.find("P")!=std::string::npos&&original_signature.find("K")!=std::string::npos,"IR invocation and identifier-role inventory");
    expect(minify::javascript_ir_signature("label:try{if(x)throw y;switch(z){case 1:break}}catch(e){}finally{}",original_signature,err),err);
    expect(original_signature.find("S")!=std::string::npos&&original_signature.find("C")!=std::string::npos,"IR statement and lexical-scope inventory");
    expect(minify::javascript_ir_signature("async function* f(a=call()){class C{static{}get x(){return a}}}",original_signature,err),err);
    expect(original_signature.find("F")!=std::string::npos&&original_signature.find("I")!=std::string::npos,"IR function and initialization inventory");
    expect(minify::javascript_ir_signature("import x from'x';export{x};function f(){eval('x')}",original_signature,err),err);
    expect(original_signature.find("M")!=std::string::npos&&original_signature.find("B")!=std::string::npos,"IR module and dynamic-scope inventory");
    expect(minify::javascript_binding_signature("function f(longName){longName+=external;return()=>longName}",original_signature,err),err);
    expect(original_signature.find("J")!=std::string::npos,"IR reference-resolution inventory");
    expect(minify::javascript("while(condition);for(;;);do;while(condition);",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"while(condition);for(;;);do;while(condition);","semantic printer preserves loop empty statements");
    expect(minify::javascript("function f(){return\n{x:1}}\n/a/.test(x)",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function f(){return\n{x:1}}/a/.test(x)","semantic printer preserves restricted return boundary while removing declaration boundary");
    expect(minify::javascript("if(flag){work()}\nelse{other()}\nfunction next(){return 1}\nnext()",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"if(flag){work()}else{other()}function next(){return 1}next()","structured printer removes block-boundary line terminators");
    expect(minify::javascript("function grouped(longValue){return (longValue)}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function grouped($){return $}","structured printer removes redundant primary grouping");
    expect(minify::javascript("function conditions(value){if(value)return (value);return (eval)('value')}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function conditions(value){if(value)return value;return(eval)('value')}","structured printer preserves grammar and direct-eval grouping");
    expect(minify::javascript("function shorthand(errorCodes){eval('');return {errorCodes:errorCodes}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function shorthand(errorCodes){eval('');return{errorCodes}}","structured printer restores redundant shorthand properties");
    expect(minify::javascript("function publicKey(longValue){return {publicName:longValue}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function publicKey($){return{publicName:$}}","structured shorthand restoration preserves public property spelling");
    expect(minify::javascript("function mixed(longValue){const stable=external;return {stable:stable,value:longValue}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function mixed($){const _=external;return{stable:_,value:$}}","structured binding and shorthand rewrites do not overlap");
    expect(minify::javascript("function neighbors(i,b){return [{i:i-4},b.delete(\"this\"),b.return(1)]}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function neighbors(i,b){return[{i:i-4},b.delete(\"this\"),b.return(1)]}","printer excludes shorthand expressions and keyword-named method calls");
    expect(minify::javascript("function outer($){return function(longName){return $+longName}(2)}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function outer($){return function(_){return $+_}(2)}","nested allocator reserves unchanged captured spelling");
    expect(minify::javascript("const handler=log=>({get:()=>(...args)=>{const item=args[0];log.push(item)}})",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const handler=$=>({get:()=>(..._)=>{const a=_[0];$.push(a)}})","nested-arrow chains allocate parents before children");
    expect(minify::javascript("function words(value){return(value)+typeof(Infinity)+void(value)}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function words($){return $+typeof Infinity+void $}","parenthesis removal preserves keyword token boundaries");
    expect(minify::javascript("const parsed=JSON.parse(text,function(key,value,{source}){return source||value})",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const parsed=JSON.parse(text,function(a,$,{source:_}){return _||$})","later destructuring parameter preserves shorthand property spelling");
    expect(minify::javascript("function declarations(longInput){var firstValue,obj2,locale1;obj2=longInput;return obj2}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function declarations(_){var a,$,b;$=_;return $}","ordinary comma declarations are not object shorthand patterns");
    expect(minify::javascript("function await(){return value}function* g(){return '' in (yield);(yield)?yield:yield}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function await(){return value}function*g(){return''in(yield);(yield)?yield:yield}","contextual await and yield grouping is preserved");
    expect(minify::javascript("var probeHeritage,setHeritage;var cls=class C extends(probeHeritage=function(){return C},setHeritage=function(){C=null}){}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"var probeHeritage,setHeritage;var cls=class C extends(probeHeritage=function(){return C},setHeritage=function(){C=null}){}","class heritage preserves outer assignment bindings");
    expect(minify::javascript("assert.throws(TypeError,()=>{var C=class extends(async()=>{}){}})",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"assert.throws(TypeError,()=>{var C=class extends(async()=>{}){}})","async arrow prefix is not treated as a binding");
    expect(minify::javascript("async function consume(longItems){for await(const {longValue=probe()} of longItems){use(longValue)}}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"async function consume(longItems){for await(const{longValue=probe()}of longItems){use(longValue)}}","for-await destructuring retains conservative binding barrier");
    expect(minify::javascript("let f=()=>{import.source(obj).catch(error=>{assert.sameValue(error,'custom error')})}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"let f=()=>{import.source(obj).catch(error=>{assert.sameValue(error,'custom error')})}","dynamic import retains conservative binding barrier");
    expect(minify::javascript("function __cont(){function __func(){return 1}if(delete __func)throw Error();return __func()}__cont()",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function __cont(){function $(){return 1}if(delete $)throw Error();return $()}__cont()","unresolved same-name occurrences block only affected function mangling");
    expect(minify::javascript("function nested(flag){let value=1;if(flag){let value=2;use(value)}return value}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function nested($){let value=1;if($){let value=2;use(value)}return value}","same-spelling nested bindings remain conservative");
    expect(minify::javascript("function thrower(){throw Error()}var f=({[thrower()]:x}={})=>{}",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"function thrower(){throw Error()}var f=({[thrower()]:x}={})=>{}","script-level function declaration remains externally named");
    expect(minify::javascript("const half=0.5,tiny=0.0001,whole=10.5;",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const half=.5,tiny=.0001,whole=10.5","JavaScript decimal fraction literal shortening");
    expect(minify::javascript("const quotes=\"can't\";",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const quotes=\"can't\"","JavaScript string quote normalization remains costed");
    expect(minify::javascript("const callback=function(){}\n(callback)()",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const callback=function(){}\n(callback)()","structured printer preserves expression-body ASI boundary");
    expect(minify::javascript("const callback=async()=>{}\ncallback()",structured,err,{minify::OptimizationLevel::Structured}),err);
    eq(structured,"const callback=async()=>{}\ncallback()","structured printer preserves arrow-body ASI boundary");
    expect(minify::javascript_effect_signature("async function f(xs){for(const x of xs)await new Box(x);return x}",original_signature,err),err);
    expect(original_signature.find("A;")!=std::string::npos&&original_signature.find("I;")!=std::string::npos&&original_signature.find("S;")!=std::string::npos&&original_signature.find("X;")!=std::string::npos,"composable effect lattice inventory");
    expect(minify::javascript_effect_signature("0;2;0n;'x';false",original_signature,err),err);
    expect(original_signature.find("V")!=std::string::npos&&original_signature.find("Q")!=std::string::npos,"primitive value and truthiness facts");
    expect(minify::javascript_effect_signature("!a;a+b;a*b;a<b;a[key]",original_signature,err),err);
    expect(original_signature.find("Z")!=std::string::npos,"abstract conversion inventory");
    expect(minify::javascript_effect_signature("obj.x;obj[key];obj?.x;delete obj.x;#x in obj",original_signature,err),err);
    expect(original_signature.find("Y")!=std::string::npos,"property effect inventory");
    expect(minify::javascript_effect_signature("function local(){}local();unknown();new Box()",original_signature,err),err);
    expect(original_signature.find("L;")!=std::string::npos&&original_signature.find("C;")!=std::string::npos&&original_signature.find("N;")!=std::string::npos,"resolved and unknown invocation inventory");
    expect(minify::javascript_ir_signature("const x={a:1,...b},y=[...x],z=`v${x}`",original_signature,err),err);
    expect(original_signature.find("O")!=std::string::npos&&original_signature.find("R")!=std::string::npos&&original_signature.find("G")!=std::string::npos&&original_signature.find("D")!=std::string::npos,"construction and spread inventory");
    expect(minify::javascript_ir_signature("const {a} = source;let [b]=items;function f(c=next(),...rest){}",original_signature,err),err);
    expect(original_signature.find("H")!=std::string::npos&&original_signature.find("I")!=std::string::npos,"destructuring and default inventory");
    expect(minify::javascript_effect_signature("const f=function(){};class C extends base{static{x()}[key]=value}",original_signature,err),err);
    expect(original_signature.find("A;")!=std::string::npos,"function and class allocation effects");
    expect(minify::javascript_effect_signature("if(true)x();while(false)y();if(flag)z()",original_signature,err),err);
    expect(original_signature.find("B2:T;")!=std::string::npos&&original_signature.find(":F;")!=std::string::npos&&original_signature.find(":?;")!=std::string::npos,"branch path facts");
    const std::vector<std::pair<std::string,std::string>> effect_alpha_cases = {
        {"function f(longName){return proxy[longName]}","function f($){return proxy[$]}"},
        {"function f(longName){return object.value+longName}","function f($){return object.value+$}"},
        {"function f(longName){return call(longName)}","function f($){return call($)}"},
        {"function f(longName){if(longName)throw error;return longName}","function f($){if($)throw error;return $}"}
    };
    for (const auto& item : effect_alpha_cases) {
        expect(minify::javascript_effect_signature(item.first,original_signature,err),err);
        expect(minify::javascript_effect_signature(item.second,renamed_signature,err),err);
        eq(renamed_signature,original_signature,"effect oracle alpha-equivalent trace");
    }
    expect(minify::javascript_cfg_signature("a();return b",original_signature,err),err);
    expect(original_signature.find("E0;")!=std::string::npos&&original_signature.find("N")!=std::string::npos&&original_signature.find("X")!=std::string::npos,"CFG entry and exits");
    expect(minify::javascript_cfg_signature("a&&b;c||d;e??f;g?h:i",original_signature,err),err);
    expect(original_signature.find("1>4;")!=std::string::npos&&original_signature.find("11>14;")!=std::string::npos&&original_signature.find("16>19;")!=std::string::npos,"CFG short-circuit and conditional edges");
    expect(minify::javascript_cfg_signature("while(x){work()}after();for(;;);done()",original_signature,err),err);
    expect(original_signature.find("8>1;")!=std::string::npos&&original_signature.find("18>14;")!=std::string::npos,"CFG loop back edges");
    minify::Options structured_jsx;structured_jsx.optimization=minify::OptimizationLevel::Structured;structured_jsx.structured_jsx_expressions=true;
    expect(minify::jsx("const view=<Card value={(function total(longName){return longName})(3)} />;",structured,err,structured_jsx),err);
    eq(structured,"const view=<Card value={(function _($){return $})(3)} />;","explicit structured JSX expression optimization");
    expect(minify::javascript("const n=(200+30);",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"const n=230","aggressive exact integer constant folding");
    expect(minify::javascript("const a=(100%7);const b=(255&15);const c=(8|3);const d=(7^3);const e=(1<2);",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"const a=2;const b=15;const c=11;const d=4;const e=(!0)","aggressive exact integer operator folding");
    expect(minify::javascript("const a=(100/4);const b=(100/3);",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"const a=25;const b=(100/3)","aggressive costed exact division compression");
    expect(minify::javascript("function f(){return undefined;}function g(undefined){return undefined;}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function f(){return void 0}function g($){return $}","aggressive unresolved undefined compression");
    expect(minify::javascript("undefined.value;undefined();undefined?.value;undefined=1;",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"(void 0).value;(void 0)();(void 0)?.value;undefined=1","aggressive undefined context preservation");
    expect(minify::javascript("const n=(9007199254740991+1);",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"const n=(9007199254740991+1)","aggressive unsafe integer fold exclusion");
    expect(minify::javascript("const n=true?123:456;const s=false?'a':'b';",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"const n=123;const s='b'","aggressive constant conditional simplification");
    expect(minify::javascript("const n=true?sideEffect():456;",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"const n=!0?sideEffect():456","aggressive effectful conditional exclusion");
    expect(minify::javascript("const n=false??true?0:42;",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"const n=!1?" "?!0?0:42","aggressive nested conditional precedence exclusion");
    expect(minify::javascript("const a=true&&longValue;const b=false||otherValue;const c=false&&unusedValue;const d=true||unusedValue;",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"const a=longValue;const b=otherValue;const c=!1;const d=!0","aggressive constant logical simplification");
    expect(minify::javascript("function f(){return 7;debugger;}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function f(){return 7}","aggressive unreachable debugger elimination");
    expect(minify::javascript("function f(){return 7;42;'unused';}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function f(){return 7}","aggressive unreachable literal statement elimination");
    expect(minify::javascript("function choose(conditionValue){if(conditionValue)return firstValue;return secondValue;}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function choose($){return $?firstValue:secondValue}","aggressive return conditional compression");
    expect(minify::javascript("function choose(conditionValue){if(conditionValue)return 1;else return 2;}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function choose($){return $?1:2}","aggressive else-return conditional compression");
    expect(minify::javascript("function add(longValue){longValue=longValue+2;return longValue;}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function add($){$+=2;return $}","aggressive local compound assignment compression");
    expect(minify::javascript("function f(){var first;var second;return 1;}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function f(){return 1}","aggressive unused declarations before joining");
    expect(minify::javascript("function f(){var unusedValue;return 1}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function f(){return 1}","aggressive unused empty var elimination");
    expect(minify::javascript("function f(){var currentValue;currentValue=1;currentValue=2;return currentValue}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function f(){var $;$=2;return $}","aggressive adjacent dead var store elimination");
    expect(minify::javascript("function f(){var first=call(1);var second=call(2);return first+second;}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function f(){var $=call(1),_=call(2);return $+_}","aggressive initialized var declaration join");
    expect(minify::javascript("function f(values){for(var key in values)use(key);var result=1;return result;}",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"function f($){for(var _ in $)use(_);var a=1;return a}","aggressive for-in declaration boundary");
    expect(minify::javascript("const answer=(function(){return 42;})();",aggressive,err,{minify::OptimizationLevel::Aggressive}),err);
    eq(aggressive,"const answer=42","aggressive literal IIFE compression");
    minify::Options bisect_options(minify::OptimizationLevel::Aggressive);
    bisect_options.disabled_javascript_passes = {
        minify::JavaScriptOptimizationPass::BindingRename,
        minify::JavaScriptOptimizationPass::ConstantFold
    };
    expect(minify::javascript("function total(longName){return (200+30)+longName;}",aggressive,err,bisect_options),err);
    eq(aggressive,"function total(longName){return(200+30)+longName}","independent JavaScript pass disablement");
    minify::Options property_options;property_options.optimization=minify::OptimizationLevel::Aggressive;property_options.property_mangle_allowlist={"internalValue"};
    expect(minify::javascript("const box={internalValue:3};box.internalValue;box.publicValue;",aggressive,err,property_options),err);
    eq(aggressive,"const box={$:3};box.$;box.publicValue","explicit property allowlist mangling");
    minify::Options aggressive_jsx(minify::OptimizationLevel::Aggressive);aggressive_jsx.structured_jsx_expressions=true;
    expect(minify::jsx("const view=<Card value={(200+30)} />;",aggressive,err,aggressive_jsx),err);
    eq(aggressive,"const view=<Card value={(230)} />;","explicit aggressive JSX expression folding");


    std::cout << "Standalone minifier smoke test passed\n";
}
