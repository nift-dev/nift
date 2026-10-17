# Hashing API: `sha256()` and `sha256_file()`

Nift has two native SHA-256 (FIPS 180-4) built-ins. One hashes a value you
already hold; the other hashes a file's bytes without loading the file. Both
return the digest as 64 lowercase hexadecimal characters, the spelling used by
`sha256sum`, release manifests and most tooling.

```nift
sha256("abc")
# ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad

sha256_file("dist/nift-linux-x64.tar.gz")
# digest of the archive's raw bytes, streamed from disk
```

## `sha256(value)` -> string

Hashes the *contents* of a string or bytes value.

- **String:** the string's UTF-8 bytes exactly as stored. No normalisation of
  any kind: line endings (`\r\n` vs `\n`), trailing whitespace and Unicode
  forms (composed `é` vs `e` + combining accent) produce different digests.
- **Bytes:** the raw bytes, including NUL bytes. `sha256(bytes([97, 98, 99]))`
  equals `sha256("abc")`.
- Anything else (numbers, booleans, `null`, arrays, objects, functions and
  other handles) is an error: `sha256: expected a string or bytes value`.
  Convert explicitly first, e.g. `sha256(n.stringify())` or
  `sha256(obj.stringify())`, so the hashed spelling is visible in the program.

## `sha256_file(path)` -> string

Hashes the file's raw bytes from the start to end-of-file.

```nift
manifest := []
for(name : ls("dist")) {
    manifest.push(name + "  " + sha256_file("dist/" + name))
}
```

- The file is read in fixed 64 KiB chunks and hashed incrementally: memory use
  does not grow with file size, and nothing is decoded or normalised. Binary
  files, NUL bytes and invalid UTF-8 are ordinary data.
- `sha256_file(p)` equals `sha256(open_bytes(p))`, without holding the file in
  memory. It is *not* `sha256(p)`, which hashes the path text.
- **Paths:** resolved like every other filesystem built-in — relative paths
  against the current directory in scripts (the project root during a build),
  absolute paths as given, `~` in standalone scripts. Paths outside the
  project work (Runtime 2.0 D-002); `--fs-root` restricts them only when
  requested. Symlinks are followed; on Windows native wide-character paths are
  used.
- **Errors** follow `open()` / `open_bytes()`:

  | Situation | Result |
  | --- | --- |
  | missing file, missing parent, dangling symlink | recoverable Error, code `io.open_failed`, `sha256_file: cannot open path` |
  | permission denied or another open failure | recoverable Error, code `io.open_failed`, same message |
  | I/O error while reading | recoverable Error, code `io.read_failed`, `sha256_file: input failure` |
  | path is a directory | error `sha256_file: path is a directory` |
  | path is not a string | error `sha256_file: expected string path` |

  ```nift
  try { digest = sha256_file("optional.bin") } catch(e) { digest = "" }  # e.code == "io.open_failed"
  ```

  Special files are read like any other file (`sha256_file("/dev/stdin")`
  hashes standard input until end-of-file; a FIFO blocks until a writer
  appears), matching `sha256sum`.

## In website builds

A page that calls `sha256_file()` depends on that file: in every incremental
mode, editing the file rebuilds the page and an unchanged file does not.
Hash and hybrid modes snapshot the bytes that were hashed (computed in the
same pass, so the file is read once). A missing file whose error the page
catches is recorded as absent, so the page rebuilds when the file appears.
`sha256()` of a value read with `open()`/`open_bytes()` is tracked through
that read.

## Name precedence

`sha256` and `sha256_file` are new names. If a program already defines a
function (or module or host callable) with one of these names, the program's
own definition is called, at top level and in loops alike; nothing changes for
existing programs.

## Not provided (yet)

- A raw-digest (bytes) variant. Hexadecimal is the interchange format for
  manifests and verification; a bytes-returning form can be added later under
  a separate name if binary protocols need it.
- Other algorithms, HMAC, keyed or streaming hash objects. `sha256` is a
  content-integrity primitive, not a cryptography toolkit.
- Directory digests: hash files individually and combine the sorted
  `name  digest` lines with `sha256()`, which makes the manifest format
  explicit.

## Implementation notes

`src/Sha256.{h,cpp}` holds the one implementation, shared by these built-ins
and intended for the Runtime 2.0 VM native table. The block transforms are a
vendored subset of Bitcoin Core's SHA-256 (MIT, see
`third_party/bitcoin-sha256/` and `THIRD_PARTY_NOTICES.md`); on x86 CPUs with
SHA extensions a hardware transform is selected at run time after a self-test,
with the portable transform as fallback.
