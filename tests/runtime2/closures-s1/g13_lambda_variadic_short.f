f := (left_arg,right_arg,...rest_args) => left_arg + right_arg + rest_args.length()
fn(observe_arg()) { print("operand evaluated"); return 2 }
alias := f
print("entered")
print(alias(observe_arg()))
