# Homebrew formula source

Nift is published as `nift` in the external `Homebrew/homebrew-core` repository.
That repository's formula is authoritative for `brew install nift`.

`nift.rb.in` is the upstream formula template for the current C++ rewrite. The
Homebrew workflow replaces `@URL@` and `@SHA256@` from an immutable tagged source
archive, then builds and tests it on macOS and Linux. The generated formula is
retained as a workflow artifact for the `homebrew-core` update pull request.

Do not copy the old LuaJIT dependency, patches, `nsm` command, or
`nifty-site-manager/nsm` source URL into a new release. They belong to the legacy
3.0.3 implementation currently published by Homebrew and are not used by the
rewrite.
