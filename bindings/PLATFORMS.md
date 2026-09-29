# Binding platform contract

- Go and C# bindings support Linux, macOS, and Windows using the native Nift C
  ABI artifact for the target platform.
- The direct Node and Python source-build scripts support Linux and macOS. They
  consume POSIX toolchains and the host runtime's Unix-style extension ABI.
- Windows Node and Python consumers are not currently shipped or supported by
  these direct scripts. Supporting them requires dedicated MSVC-compatible
  addon/extension builds and packaging rather than treating MinGW output as a
  valid CPython or Node binary.

Cross-platform Gate jobs must build the bindings supported by each runner and
audit every resulting native artifact that contains vendored libffi.
