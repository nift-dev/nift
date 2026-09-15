#!/usr/bin/env python3
"""CP28 v4.2 performance/memory certification: workload fixtures.

Generates the representative workload fixtures and returns a mapping of
workload name -> (fixture_root, expected_marker). Fixtures are classified as
v4.1-only (runnable on both the pre-v4.2 baseline and the v4.2 candidate) or
v4.2-only (candidate only). Each fixture is a single-page project whose page
amplifies one feature so `nift build --all` cost is dominated by that feature
rather than process startup.
"""
import json
import pathlib

WORKLOAD_DEFS = {
    "render": {"v41": True},
    "decl_assign": {"v41": True},
    "expr": {"v41": True},
    "fn_v41": {"v41": True},
    "fragment_v41": {"v41": True},
    "nested_v41": {"v41": True},
    "input_v41": {"v41": True},
    "json_v41": {"v41": True},
    "while_v42": {"v41": False},
    "fnwhile_v42": {"v41": False},
    "fnfor_v42": {"v41": False},
    "struct_v42": {"v41": False},
    "method_v42": {"v41": False},
    "this_v42": {"v41": False},
    "private_v42": {"v41": False},
    "alias_v42": {"v41": False},
    "copy_v42": {"v41": False},
    "deepcopy_v42": {"v41": False},
}


def _project(root: pathlib.Path):
    (root / ".nift").mkdir(parents=True)
    (root / "content").mkdir()
    (root / "templates").mkdir()
    (root / "public").mkdir()
    (root / "data").mkdir()
    (root / ".nift/config.json").write_text(json.dumps({
        "config": {
            "content-dir": "content/", "content-ext": ".html",
            "output-dir": "public/", "output-ext": ".html",
            "default-template": "templates/template.html",
            "build-threads": -1, "incremental-mode": "modified",
            "minify-exts": [],
        }}, separators=(",", ":")))
    (root / ".nift/tracked.json").write_text(json.dumps({"tracked": [
        {"name": "/", "title": "bench", "template": "templates/template.html"}
    ]}, separators=(",", ":")))


def _content(root: pathlib.Path, text: str):
    (root / "content" / "index.html").write_text(text)


def _template(root: pathlib.Path, text: str):
    (root / "templates" / "template.html").write_text(text)


def _data(root: pathlib.Path, name: str, text: str):
    (root / "data" / name).write_text(text)


def _renders(*markers) -> str:
    return "\n".join(markers)


def make_render(root):
    _project(root); _template(root, "<!doctype html><html><body>\n@content\n</body></html>\n")
    _content(root, "".join(f"<p>item {i}</p>\n" for i in range(2000)))
    return "item 1999"


def make_decl_assign(root):
    _project(root); _template(root, "@content\n")
    _content(root, "$[v := 0]\n" + "$[v = v + 1]\n" * 500 + "v=$[v]\n")
    return "v=500"


def make_expr(root):
    _project(root); _template(root, "@content\n")
    _content(root, "$[a := 1]\n" + "$[a * 2 + 3 == 5]" * 400)
    return "true" * 400


def make_fn_v41(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@fn(add(a,b)){@return(a+b)}\n@fn(mul(a,b)){@return(a*b)}\n" + "$[add(3,4)]$[mul(5,6)]" * 400)
    return "730" * 400


def make_fragment_v41(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@fragment(card(t)){<div class=\"c\">$[t]</div>}\n" + "$[card(\"hi\")]" * 300)
    return "<div class=\"c\">hi</div>" * 300


def make_nested_v41(root):
    _project(root); _template(root, "@content\n")
    body = "@for(i : items.items) {\n@for(j : [1,2,3,4,5]) {\n@if(i % 2 == 0) { <b>$[i]-$[j]</b> }\n}\n}\n"
    _content(root, "@json(items, \"data/items.json\")\n" + body)
    _data(root, "items.json", json.dumps({"items": list(range(100))}))
    return "<b>0-5</b>"


def make_input_v41(root):
    _project(root)
    _template(root, "@content\n" + "".join(f"@input(\"p{i}.html\")\n" for i in range(40)))
    _content(root, "<p>base</p>\n")
    for i in range(40):
        (root / "templates" / f"p{i}.html").write_text(f"<span>{i}</span>\n")
    return "<span>39</span>"


def make_json_v41(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@json(site, \"data/site.json\")\n" + "$[site.items[0].name]=$[site.items[1].value]" * 300)
    _data(root, "site.json", json.dumps({"items": [{"name": "a"}, {"value": 42}]}))
    return "a=42" * 300


def make_while_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root, "$[i := 0]\n@while(i < 600) { $[i = i + 1] }\ni=$[i]\n")
    return "i=600"


def make_fnwhile_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@fn(count(n)) {\n  i := 0\n  while(i < n) { i = i + 1 }\n  return i\n}\n" + "$[count(600)]\n")
    return "600"


def make_fnfor_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@fn(sum(n)) {\n  t := 0\n  for(i : [1,2,3,4,5,6,7,8,9,10]) { if(i % 2 == 0) { t = t + i } }\n  return t\n}\n" + "$[sum(0)]" * 300)
    return "30" * 300


def make_struct_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@struct(point) {\n  x := 0\n  y := 0\n  fn(point(x_, y_)) { x = x_; y = y_ }\n  fn(x()) { return x }\n}\n"
        + "".join(f"$[p{i} := point({i}, {i + 1})]" for i in range(300))
        + "".join(f"$[p{i}.x()]" for i in range(300)) + "\n")
    return "".join(str(i) for i in range(300))


def make_method_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@struct(counter) {\n  private n := 0\n  fn(add(k)) { n = n + k }\n  fn(value()) { return n }\n}\n"
        + "$[c := counter()]\n" + "$[c.add(1)]\n" * 500 + "$[c.value()]\n")
    return "500"


def make_this_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@struct(counter) {\n  value := 0\n  fn(set(v)) { this.value = v }\n  fn(get()) { return this.value }\n}\n"
        + "$[c := counter()]\n" + "$[c.set(7)]\n" * 200 + "$[c.get()]\n")
    return "7"


def make_private_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@struct(box) {\n  private secret := 99\n  fn(peek()) { return secret }\n  fn(add(n)) { secret = secret + n }\n}\n"
        + "$[b := box()]\n" + "$[b.add(1)]\n" * 200 + "$[b.peek()]\n")
    return "299"


def make_alias_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@struct(node) { v := 0 }\n"
        + "$[a := node()]\n$[b := a]\n" + "$[b.v = b.v + 1]\n" * 300 + "v=$[a.v]\n")
    return "v=300"


def make_copy_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root, "@struct(cell) { v := 0 }\n"
        + "$[c := cell()]\n" + "$[c = copy(c)]\n" * 300 + "v=$[c.v]\n")
    return "v=0"


def make_deepcopy_v42(root):
    _project(root); _template(root, "@content\n")
    _content(root,
        "@struct(leaf) { v := 0 }\n"
        "@struct(branch) { a := leaf()\nb := leaf() }\n"
        "@struct(top) { left := branch()\nright := branch() }\n"
        + "$[t := top()]\n" + "$[t = deepcopy(t)]\n" * 150
        + "ok=$[t.left.a.v]$[t.right.b.v]\n")
    return "ok=00"


MAKERS = {
    "render": make_render,
    "decl_assign": make_decl_assign,
    "expr": make_expr,
    "fn_v41": make_fn_v41,
    "fragment_v41": make_fragment_v41,
    "nested_v41": make_nested_v41,
    "input_v41": make_input_v41,
    "json_v41": make_json_v41,
    "while_v42": make_while_v42,
    "fnwhile_v42": make_fnwhile_v42,
    "fnfor_v42": make_fnfor_v42,
    "struct_v42": make_struct_v42,
    "method_v42": make_method_v42,
    "this_v42": make_this_v42,
    "private_v42": make_private_v42,
    "alias_v42": make_alias_v42,
    "copy_v42": make_copy_v42,
    "deepcopy_v42": make_deepcopy_v42,
}


def make_workload(name: str, root: pathlib.Path) -> str:
    if name not in MAKERS:
        raise KeyError(name)
    return MAKERS[name](root)