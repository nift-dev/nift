# Advanced templating — Nift v4.1 development

Most Nift projects should continue to centre `@input`, `@content` and `@path`. The v4.1 expression features are optional tools for templates that genuinely need state or reusable computation.

```text
$[x := 10]          # declare; renders nothing
$[x = 20]           # assign nearest existing mutable binding
$[const x := 10]    # shallow const binding
$[immut x := {...}] # deep-readonly view
@:=(items){[1,2,3]} # multiline declaration
```

Bindings have stable inferred types. Child scopes (`@if`, `@for`, `@input`, injection and callable calls) see caller-visible bindings and can mutate existing mutable outer bindings; declarations remain local.

`inject(path)` supplies expression data from a tracked project file. `validate(schema, value)` validates and returns its value. Legacy `@json` remains supported during the v4.1 migration.

Value helpers and rendered helpers are distinct:

```text
@fn(add(a,b)){ @return(a+b) }
@fragment(card(text)){ <div>$[text]</div> }

$[add(2,3)]
$[card("hello")]
```

User callables are invoked only inside `$[...]`; this preserves passthrough of unrelated source-language constructs such as CSS `@media`.
