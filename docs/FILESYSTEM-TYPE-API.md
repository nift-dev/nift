# Filesystem type inspection API

Nift exposes three filesystem type-inspection primitives plus the long-standing
`exists()`. They share one internal metadata query
(`std::filesystem::status(path, error_code)`) so `is_file`, `is_dir`, `stat`
and `exists` never issue separate filesystem probes for the same answer, and
they follow the same path resolution and project/`--fs-root` confinement model
as every other filesystem built-in.

## `exists(path)` -> bool

```nift
exists("data.txt")       # true for a regular file
exists("assets")         # true for a directory
exists("missing.txt")    # false
```

`exists()` reports whether the path resolves to anything (file, directory,
symlink target). A missing path returns `false`; a genuine metadata failure
(permission denied, inaccessible parent, platform-invalid path) surfaces a
recoverable Error.

## `is_file(path)` -> bool

```nift
is_file("data.txt")      # true
is_file("assets")        # false (a directory)
is_file("missing.txt")   # false
```

`true` only for regular files. A symlink to a regular file counts as a file
(symlinks are followed). Dangling symlinks and missing paths return `false`.

## `is_dir(path)` -> bool

```nift
is_dir("assets")         # true
is_dir("data.txt")       # false (a regular file)
is_dir("missing.txt")    # false
```

`true` only for directories. A symlink to a directory counts as a directory
(symlinks are followed).

## `stat(path)` -> object

```nift
info := stat("data.txt")
info.exists              # bool
info.type                # "file" | "directory" | "other"
info.size                # bytes (regular files only)

if(info.exists && info.type == "directory") { ... }
```

`stat()` returns `{ exists: true, type: "...", size: N }` for an existing
path and `{ exists: false }` for a missing path. It follows symlinks, matching
the predicates: a symlink to a file reports `type == "file"` and the resolved
target's size. `size` is only meaningful for regular files; directories and
other types carry no size key.

## Semantics

- **Nonexistent path:** `exists`/`is_file`/`is_dir` return `false`; `stat`
  returns `{ exists: false }`. This is not an error.
- **Symlinks:** all four follow symlinks, so `is_file(symlink_to_file)` is
  `true` and `is_dir(symlink_to_directory)` is `true`. A dangling symlink is
  treated as nonexistent. Distinguishing a symlink from its target (lstat-style
  metadata) is a deliberate future extension.
- **Recoverable errors:** ordinary missing-path and wrong-type cases return
  `false`. A genuine filesystem metadata failure (permission denied,
  inaccessible parent, path-invalid on the platform) raises a recoverable
  Error (`io.metadata_failed`), catchable with `try/catch`. Note that Windows
  classifies some invalid-name paths as "not found" and therefore returns
  `false` for those, while POSIX surfaces them as errors.
- **Paths:** relative paths resolve against the current directory (or the host
  root under a project host); absolute paths are used as-is; `~` expands in
  standalone scripts. Spaces, Unicode, nested paths, `./`, `../`, and trailing
  separators are handled. No slash or extension heuristics are used.
- **Confinement:** identical to other filesystem built-ins — the project root
  and `--fs-root` containment rules apply.

## Windows

Windows path forms are supported directly:

```nift
is_file("C:\\Program Files\\example\\app.exe")
is_dir("C:/Users/alice/Documents")            # forward slashes work
is_dir("C:\\Users\\alice\\Documents\\")       # trailing backslash is fine
```

Drive-letter absolute paths, backslashes, forward slashes, spaces, Unicode and
trailing directory separators are all handled by the shared metadata query.