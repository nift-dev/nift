# Nift vendoring provenance

- Component: libffi
- Version: 3.8.0
- Upstream release: https://github.com/libffi/libffi/releases/tag/v3.8.0
- Upstream commit: `12ffd1f9dc56fcea79d2f742f424301ae668d663`
- Release source: https://github.com/libffi/libffi/releases/download/v3.8.0/libffi-3.8.0.tar.gz
- Release source SHA-256: `7da3e2d9a171eb0a038f592ecad3ff2bb2550f3496d87b3b29ad0cf4430c0db4`
- Vendored tree SHA-256: `5dbcaaf332ef970cba42bca8e65a0c39c6b72d1550f4ef67bbc35c3ed09e77b8`
- Retrieved/verified: 2026-09-29

The checksum is the digest published by GitHub for the upstream release asset.
Nift builds this source out of tree; generated target headers and objects stay
under `.build/libffi` and are not vendored.
