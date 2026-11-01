// Review round 3 (coordinator, D-NEXT-refsem): an object holding an instance. The VM's serializer
// cannot render a struct instance inside a container and inference does not
// track struct kinds in contents, so in a program that declares a struct a
// presentation method on a value that may be a container is refused at
// compile time, before the first print (conservative; legacy prints
// `{"k":C{v:0}}`). A Phase B / VM capability candidate, not a permanent limitation.
struct(C) {
  v := 0
}
print("before")
o := {"k": C()}
print(o.stringify())
