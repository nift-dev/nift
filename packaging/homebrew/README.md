# Homebrew formula source

Nift is published as `nift` in the external `Homebrew/homebrew-core` repository.
That repository's formula is authoritative for `brew install nift`.

`nift.rb.in` is the upstream formula template for the current C++ rewrite. The
Homebrew workflow replaces `@URL@` and `@SHA256@` from an immutable tagged source
archive, then builds and tests it on macOS and Linux. The generated formula is
retained as a validation artifact; Homebrew's automation owns ordinary
`homebrew-core` version bumps, so do not use the artifact to open a manual simple
bump pull request.

Do not reintroduce the old LuaJIT dependency, patches, `nsm` command, or
`nifty-site-manager/nsm` source URL. They belong to the legacy 3.0.3 formula and
are not used by the rewrite. As rechecked on 19 August 2026, Homebrew's canonical
formula publishes Nift 4.0.1; future ordinary updates must follow Homebrew's
supported version-bump automation and then be verified through the public formula.
