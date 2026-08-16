# Flathub update source

Nift already exists on Flathub as `cc.nift.nsm`. Its authoritative manifest is
maintained in `flathub/cc.nift.nsm`; it is intentionally not published from this
directory.

`cc.nift.nsm.json.in` is an upstream migration aid for the C++ rewrite. Replace
`@VERSION@` and `@SHA256@` with the released source archive version and checksum,
then reconcile the result in the Flathub repository. Preserve the existing app
ID and its established AppStream, desktop and icon assets. The current Flathub
manifest builds the legacy 2.4.12 `nsm-flatpak` repository, bundles Git and
LuaRocks, and applies a legacy patch; those inputs must be removed or reviewed
rather than carried into the rewrite mechanically.

The candidate deliberately does not duplicate the Flathub-owned store metadata.
Run `flatpak-builder` and Flathub linting against the complete external repository
before opening an update pull request.
