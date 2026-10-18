fn(load()) {
    a := 1
    text := cat("/nonexistent/raised-on-line-3")
    return text
}
// blank
try {
    b := 2
    load()
} catch(err) {
    print(err.code)
    print(err.line.to_string() + ":" + err.column.to_string())
}
