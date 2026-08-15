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

    expect(minify::html("  <div   class=\"a  b\">  hello   world <!-- gone --> <span> x </span> </div>  ", out, err), err);
    eq(out, "<div class=\"a  b\"> hello world <span> x </span> </div>", "html basic");
    expect(minify::html("<pre>  a\n    b </pre><script> const x = ` a  b `;\n</script>", out, err), err);
    expect(out.find("<pre>  a\n    b </pre>") != std::string::npos, "pre contents changed");
    expect(out.find("<script> const x = ` a  b `;\n</script>") != std::string::npos, "script contents changed");
    expect(minify::html("a<!--[if IE]>x<![endif]-->b", out, err), err);
    expect(out.find("<!--[if IE]>") != std::string::npos, "conditional comment removed");

    expect(minify::javascript("const  x = 1; // comment\nconst y = x + 2;\n", out, err), err);
    expect(out.find("// comment") == std::string::npos, "JS line comment retained");
    expect(out.find('\n') != std::string::npos, "JS newline removed");
    expect(minify::javascript("const r = /https?:\\/\\/example\\.com/; /*x*/\nconst t=` a  b `;", out, err), err);
    expect(out.find("/https?:\\/\\/example\\.com/") != std::string::npos, "JS regex damaged");
    expect(out.find("` a  b `") != std::string::npos, "JS template damaged");
    expect(minify::javascript("return\n  value;", out, err), err);
    expect(out.find("return\n") != std::string::npos, "ASI-sensitive newline removed");


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

    // HTML: preserve whitespace text nodes rather than guessing display mode,
    // preserve raw-text comments literally, malformed input must fail cleanly.
    expect(minify::html("<span>a</span> <span>b</span><div> c </div>", out, err), err);
    expect(out.find("</span> <span>") != std::string::npos, "HTML inline inter-element space erased");
    expect(minify::html("<script><!-- not an html comment --></script><style>/* raw */ .x { a: b; }</style>", out, err), err);
    expect(out.find("<!-- not an html comment -->") != std::string::npos, "HTML raw script text changed");
    expect(out.find("/* raw */") != std::string::npos, "HTML raw style text changed");
    expect(!minify::html("<div class=\"x\"", out, err), "malformed HTML tag accepted");
    expect(err.find("unterminated HTML tag") != std::string::npos, "malformed HTML error missing");
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

    // Malformed lexical constructs should fail cleanly rather than silently
    // emitting a half-minified program.
    expect(!minify::javascript("const x = 'unterminated", out, err), "unterminated JS string accepted");
    expect(!minify::javascript("const x = `unterminated", out, err), "unterminated JS template accepted");
    expect(!minify::css("a{/* unterminated", out, err), "unterminated CSS comment accepted");

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
    expect(out.find("0x1 .toString") != std::string::npos, "hex numeric/member boundary collapsed");
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

    // Malformed inputs fail rather than emitting guessed output.
    expect(!minify::html("<div", out, err), "unterminated HTML tag accepted");
    expect(!minify::css("a{/*", out, err), "unterminated CSS comment accepted");
    expect(!minify::javascript("/*", out, err), "unterminated JS comment accepted");

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
    idem_fn(minify::jsx, "const x=<div> hello  world </div>;", "JSX");


    // JSX: preserve text/markup spelling, but minify embedded JS expressions and
    // ordinary JS surrounding the JSX region.
    expect(minify::jsx("const  el = <div className=\"x\"> hello  world {  value +  1  } </div> ;", out, err), err);
    expect(out.find("const el=") != std::string::npos, "JS around JSX was not minified");
    expect(out.find(" hello  world ") != std::string::npos, "JSX text whitespace changed");
    expect(out.find("{value+1}") != std::string::npos, "JSX expression was not minified");
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
    expect(minify::svg("<svg xmlns=\"http://www.w3.org/2000/svg\">\n <text>hello   world</text>\n <path d=\"M 0 0 L 10 10\" />\n</svg>", out, err), err);
    expect(out.find("hello   world") != std::string::npos, "SVG text whitespace changed");
    expect(out.find("M 0 0 L 10 10") != std::string::npos, "SVG path attribute changed");

    idem(minify::Format::Xml, "<root>\\n <a> text  here </a>\\n</root>", "XML");
    idem(minify::Format::Svg, "<svg>\\n<text>a  b</text>\\n</svg>", "SVG");
    idem(minify::Format::Jsx, "const x = <div>{ value + 1 }</div>;", "JSX");

    minify::Format f;
    expect(minify::format_for_extension(".html", f) && f == minify::Format::Html, "html extension");
    expect(minify::format_for_extension("MJS", f) && f == minify::Format::JavaScript, "mjs extension");
    expect(minify::format_for_extension(".jsx", f) && f == minify::Format::Jsx, "jsx extension");
    expect(minify::format_for_extension(".xml", f) && f == minify::Format::Xml, "xml extension");
    expect(minify::format_for_extension(".svg", f) && f == minify::Format::Svg, "svg extension");
    expect(!minify::format_for_extension(".ts", f), "TypeScript source extension unexpectedly supported");
    expect(!minify::format_for_extension(".tsx", f), "TSX source extension unexpectedly supported");

    std::cout << "Standalone minifier smoke test passed\n";
}
