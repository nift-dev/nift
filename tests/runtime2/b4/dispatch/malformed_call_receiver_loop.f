// B4 review M1 (approved diagnostic difference, compatibility.md): a malformed method call on
// a call receiver with arguments, in loop code. Same output, line, column and exit status as
// legacy; legacy's message shows its prepared evaluator's hoisted argument names
// (`starts_with(__nift_native_arg_5)`), the VM's the source text (golden .vm).
fn(mk(i)) { if (i == 0) { return " ab " } return null }
for (i : [0, 1]) { print(mk(i).starts_with("a")) }
