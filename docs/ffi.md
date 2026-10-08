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

FFI buffer storage belongs to the active Parser. An embedded Engine keeps its
script Parser across `execute` and `evaluate` calls, so repeated buffer creation
accumulates storage until that owner is destroyed. Dropping or rebinding a script
handle, leaving a function, clearing a container, or destroying a result does
not free the buffer. Assignment and `deepcopy` preserve opaque buffer identity;
all aliases observe native mutations to the same storage. Reuse an existing
fixed-size buffer for repeated native work where practical.

Native code may retain an address passed through a `buffer` argument. Buffer
addresses and pointer handles depend on the owning Parser/Engine lifetime;
a copied result or marker string does not extend it. Failed import rollback can
also invalidate newly created resources. There is no buffer release API;
`ffi_close` closes a library, not its buffers. Automatic reclamation and explicit
release are deferred design work beyond v4.8.

Library filenames remain platform-specific: `.so` on ELF systems, `.dylib` on
macOS, and `.dll` on Windows. A package should choose the filename from `os()`
or package configuration; Nift does not silently substitute a different native
library.

The v4.5 built-in dispatcher intentionally rejects variadics, C++ ABIs, mixed
integer/floating signatures, arbitrary by-value structs, and callback shapes
other than the documented bridge. Use pointer-to-struct/buffer APIs when a C
library offers them. Native pointers are opaque handles, not integers.
