# Third-party notices

## libffi 3.8.0

Nift vendors the unmodified libffi 3.8.0 release source under
`third_party/libffi` and statically links its target-specific build into Nift
artifacts. libffi is copyright Anthony Green, Red Hat, Inc. and other
contributors and is distributed under the permissive license reproduced with
each source or binary distribution that contains libffi. Build-tool notices are
also included with source and native distributions.

Source and verification metadata are recorded in Nift's vendored libffi
provenance file.

## Bitcoin Core SHA-256 (subset of v29.0)

Nift vendors a subset of the SHA-256 implementation from Bitcoin Core v29.0
(portable block transform, streaming driver and x86 SHA-NI transform) under
`third_party/bitcoin-sha256` and compiles it into Nift artifacts. The SHA-NI
transform is based on public-domain code by Jeffrey Walton, Intel and Sean
Gulley. Provenance and verification metadata are recorded in that directory's
provenance file. License:

```text
The MIT License (MIT)

Copyright (c) 2009-2025 The Bitcoin Core developers
Copyright (c) 2009-2025 Bitcoin Developers

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
