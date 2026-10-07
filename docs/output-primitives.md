# Output primitives (print / warn / err / throw)

The four mechanisms are distinct:

```text
print(value)    ordinary output to stdout
warn(value)     non-fatal warning, rendered "warning: <value>" to stderr
err(value)      writes a value to stderr
throw ...       failure / control flow
```

`err(value)` writes to stderr but is **not** itself `throw` — the calling
script continues. `warn(value)` is a first-class non-fatal warning: it emits
one canonical `warning: <value>` line, keeps running, never writes into normal
rendered/stdout output (or a template's rendered page), and returns `null`
exactly like `print`/`err`.

`print`, `err` and `warn` share the same value rules: exactly one argument, the
same renderable scalar class (numbers, booleans, null, text, rendered
expressions), `$[...]` template-parameter interpolation, and the same
rejections (bytes and non-renderable composite/internal values fail with the
usual `*: ...` diagnostic). An invalid call to `warn` is a normal language
error, even though the warning itself is non-fatal.

## Templates / builds

Because `warn` never changes rendered output, it is the right tool for build
and template diagnostics. A template function can warn and still render its
page unchanged:

```nift
@fn(render_deprecated()) {
    warn("old_field is deprecated; use new_field")
    return ""
}
$[render_deprecated()]Hello
```

The site builds successfully, the rendered page is unchanged, and the build
log carries one `warning: old_field is deprecated; use new_field`.

Warnings currently have no categories, levels, filtering or `-Werror`
behaviour; they are a non-fatal emission primitive, not a logging framework.