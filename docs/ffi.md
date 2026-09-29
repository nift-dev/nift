# Native C FFI

Nift v4.5 can load a native C ABI library directly from scripts.

```nift
lib := ffi_open("./libmath.so")
add := (a, b) => ffi_call(lib, "add_i64", "i64(i64,i64)", a, b)
print(add(20, 22))
```

Package authors should put signatures in one module and export ordinary Nift
functions rather than exposing `ffi_call` throughout an application. The
runtime helpers `ffi_buffer`, `ffi_bytes`, `ffi_snapshot_bytes`, `ffi_sizeof`,
`ffi_struct`, and `ffi_callback` centralise byte/layout handling.

`ffi_buffer(bytes_value)` copies immutable bytes into a new mutable FFI buffer.
`ffi_snapshot_bytes(buffer)` copies the buffer's current contents into immutable
first-class bytes. Neither bridge aliases storage. The compatibility helper
`ffi_bytes(buffer)` continues to return an integer array.

Library filenames remain platform-specific: `.so` on ELF systems, `.dylib` on
macOS, and `.dll` on Windows. A package should choose the filename from `os()`
or package configuration; Nift does not silently substitute a different native
library.

The v4.5 built-in dispatcher intentionally rejects variadics, C++ ABIs, mixed
integer/floating signatures, arbitrary by-value structs, and callback shapes
other than the documented bridge. Use pointer-to-struct/buffer APIs when a C
library offers them. Native pointers are opaque handles, not integers.
