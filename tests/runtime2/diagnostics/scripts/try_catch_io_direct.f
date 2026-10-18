// line1
// line2
try {
    c := 3
    d := cat("/nonexistent/raised-on-line-5")
} catch(err) {
    print(err.code)
    print(err.line.to_string() + ":" + err.column.to_string())
}
try { z := cat("/nonexistent/line10") } catch(e) { print(e.line.to_string() + ":" + e.column.to_string()) }
