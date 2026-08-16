# Chocolatey package source

These files are templates. `__VERSION__` and `__CHECKSUM64__` are deliberately
invalid publication values and are replaced in a temporary staging directory by
`.github/workflows/chocolatey.yml`. Do not commit a release checksum by editing
the templates after every release.

The package downloads the matching Windows x86-64 ZIP from the authoritative
GitHub release. The workflow verifies that the executable reports the expected
public version before packing, then submits the generated `.nupkg` only when the
`CHOCOLATEY_API_KEY` repository secret is configured.
