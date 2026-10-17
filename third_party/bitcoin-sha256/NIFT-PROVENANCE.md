# Nift vendoring provenance

- Component: SHA-256 from Bitcoin Core (subset)
- Upstream: https://github.com/bitcoin/bitcoin
- Release tag: `v29.0`
- Upstream commit: `f490f5562d4b20857ef8d042c050763795fd43da` (2025-04-11)
- License: MIT (`COPYING`, copied unmodified; SHA-256
  `7c4a87f43afaf667b4c2187af92ebdd27310a24cec113f973e058e3300a76002`)
- The x86 SHA-NI transform is upstream's adaptation of Jeffrey Walton's
  public-domain SHA-Intrinsics (`sha256-x86.c`), based on Intel code and Sean
  Gulley's miTLS work; upstream distributes it under MIT.
- Retrieved/verified: 2026-10-18 (sparse git clone of the tag over HTTPS)

## Why this implementation

Bitcoin Core's SHA-256 is one of the most heavily exercised SHA-256
implementations in production C++, with upstream unit tests, fuzzing and a
built-in self-test; it is permissively licensed, dependency-free, and has a
SHA-NI transform used in production. Nift needs neither its double-SHA256
multi-way kernels nor its C++20 helpers, so only the pieces below are vendored.

## Upstream files

| File | SHA-256 |
| --- | --- |
| `src/crypto/sha256.h` | `d89ad648e6fb23f32d4602446ed5a06c1da5fe89e4531d20784df1dc51e1b5f4` |
| `src/crypto/sha256.cpp` | `3c6e5cfba841062aefaf8006554ab2231e2085ace9916ea080450ca67bd15441` |
| `src/crypto/sha256_x86_shani.cpp` | `4fbf2b3e3580dfb238c59c669cf280b3f44d3f7b4fb8cc3b5507e31cd63cf017` |

## Verbatim blocks in `sha256_bitcoin.h`

Each block is a byte-for-byte copy of the upstream line range (hash of the
block text, each line newline-terminated).

| Upstream file | Lines | Block SHA-256 |
| --- | --- | --- |
| `src/crypto/sha256.h` | 13-27 | `dcce7fc6fa54884ffd74e09b6fca3cbfff8af82d55f66d9c0d0ba10091003cdd` |
| `src/crypto/sha256.cpp` | 69-185 | `f0eabc1cedce41da896dfe7f026dbfc424d4c71a2afb16230499a419cb0240ee` |
| `src/crypto/sha256.cpp` | 696-749 | `51cc817a5ff9f202b4cdfbc66e90aa97081cfcd04785e2214b0fcd70e49fc84c` |
| `src/crypto/sha256_x86_shani.cpp` | 16-77 | `1f463cd4c00658993879641ddba13a6e2fb7f1bf145e4522e5b637096dc5a4a7` |
| `src/crypto/sha256_x86_shani.cpp` | 79-142 | `a60d54d0c726920aa83c12955ec549ab6bade0312e59a992bca4d290ccb6240a` |

Contents: the `CSHA256` class declaration; the portable round functions,
`Initialize` and `Transform`; `CSHA256` constructor, `Write`, `Finalize` and
`Reset`; the SHA-NI helpers and the one-way `sha256_x86_shani::Transform`.

## Nift changes outside the verbatim blocks

- C++17 big-endian load/store helpers replace upstream's C++20
  `crypto/common.h` (`ReadBE32`, `WriteBE32`, `WriteBE64`).
- The vendored code is wrapped in `namespace nift_vendor_bitcoin` (portable
  code in `portable::sha256`, SHA-NI code in `x86_shani`).
- `Transform`, which upstream's `CSHA256` calls through a file-scope function
  pointer assigned by `SHA256AutoDetect()`, is declared here and defined in
  `src/Sha256.cpp`, which selects the transform lazily (CPUID leaf check, SHA,
  SSE4.1 and SSSE3 bits, and a known-answer self-test before trusting SHA-NI).
- Upstream compiles the SHA-NI file with `-msse4 -msha`; Nift instead enables
  `sse4.1,sha` per function with `#pragma GCC target` / `#pragma clang
  attribute`, so no build system needs per-file flags and the rest of the
  program keeps the baseline target. `ALWAYS_INLINE` is defined as in
  upstream's `attributes.h`.

## Not vendored

`SHA256AutoDetect`, `SHA256D64` and every double-SHA256 multi-way kernel
(SSE4.1 4-way, AVX2 8-way, SHA-NI 2-way), the SSE4 assembly transform, and the
ARMv8 SHA transform (not verifiable on the checkpoint host; future work).

## Verification

`python3 scripts/check_vendored_sha256.py` (run by `make test-sha256`) checks
every block hash against this file; with `--upstream <checkout of the commit
above>` it also compares each block with the upstream lines and the upstream
file hashes.
