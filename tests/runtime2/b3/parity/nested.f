// B3: a nested instance renders with its own descriptor; the outer private
// field (an instance) and the inner private field are both skipped.
struct(In) {
  v := 2
  private h := "SECRET-b3"
}
struct(Out) {
  i := ""
  private p := ""
  fn(Out()) { i = In(); p = In() }
}
x := Out()
print(x.stringify())
print(x.prettify())
print([x].prettify())
