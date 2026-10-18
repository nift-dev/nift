// Shape-only mock of the proposed process API (CP16 design §12). It launches
// nothing; it exists so every line of the syntax examples is executed by the
// legacy parser/evaluator. syntax_check.sh renames run( -> mrun( and
// cmd( -> mcmd( (legacy built-ins cannot be shadowed, D-009 #3).
struct(mproc) {
 pid := 4242
 done := false
 sent := ""
 fn(write(s)) { sent = sent + s; return null }
 fn(write_line(s)) { sent = sent + s + "\n"; return null }
 fn(read_line()) { return "2" }
 fn(read_all()) { return "" }
 fn(close_stdin()) { return null }
 fn(running()) { return !done }
 fn(cancel(...opts)) { done = true; return true }
 fn(wait(...opts)) { return {"exit_code": 143, "stdout": "", "stderr": "", "launched": true, "signal": "SIGTERM", "cancelled": done, "timed_out": false, "stages": []} }
}
fn(mresult(mode, program)) {
 if(program == "definitely-missing") { return {"exit_code": 127, "stdout": "", "stderr": "", "launched": false, "error": "executable not found: definitely-missing", "signal": null, "cancelled": false, "timed_out": false} }
 out := ""
 if(mode == "capture") { out = "hi" }
 return {"exit_code": 0, "stdout": out, "stderr": "", "launched": true, "signal": null, "cancelled": false, "timed_out": false}
}
fn(mrun(...args)) {
 last := args[args.length() - 1]
 if(type(last) == "object") {
  if(last.get("mode", "") == "interactive" || last.get("mode", "") == "detached" || last.get("wait", true) == false || last.get("detach", false)) { return mproc() }
  if(last.get("mode", "") == "stream" || last.get("stdout", "") == "inherit") { return mresult("stream", args[0]) }
 }
 return mresult("capture", args[0])
}
// Legacy cannot chain methods on a struct returned by a call (`mk().run()`
// is "unknown value or malformed expression"; built-in cmd() handles can), so
// C_* examples have a step-wise twin C_*.mock.f for the mock pass.
struct(mcmdb) {
 program := ""
 fn(stream()) { return mcmdb() }
 fn(timeout(ms)) { return mcmdb() }
 fn(kill_after(ms)) { return mcmdb() }
 fn(pipe(c)) { return mcmdb() }
 fn(start()) { return mproc() }
 fn(detach()) { return mproc() }
 fn(run()) { return mresult("capture", program) }
}
fn(mcmd(...args)) { c := mcmdb(); c.program = args[0]; return c }
url := "https://example.invalid"
