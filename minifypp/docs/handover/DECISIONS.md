# Minify++ decision ledger

## Independent project identity

**Status:** SETTLED

Minify++ is standalone. Nift embeds/uses it but does not own its product identity.

## Name and executable

**Status:** CURRENT/SETTLED

Display name Minify++, executable `minify`, C++ namespace `minify`, public header
`<minify/Minify.h>`. Sift is historical only.

## Semantic preservation priority

**Status:** SETTLED

Correctness outranks marginal compression. Conservative output is acceptable.

**Revisit if:** stronger transformations can be given clear language-aware proof,
broad corpus/runtime evidence, and acceptable maintenance complexity.

## Product scope

**Status:** SETTLED DIRECTION

Minification is the owned domain. Bundling, module resolution, transpilation,
tree shaking, and package/build orchestration are rejected current directions.

## Public format contract

**Status:** CURRENT

Format version 1 covers HTML, CSS, JavaScript, JSX, JSON, XML, and SVG under the
documented conservative behavior. XML/SVG are not claimed as fully validating
parsers.

## Destructive CLI behavior

**Status:** SETTLED/CURRENT

Default output preserves source and creates a sibling minified file. In-place
overwrite is explicit.

## Canonical synchronization

**Status:** CURRENT OPERATIONAL DIRECTION

Standalone Minify++ is the canonical development origin; Nift embeds matching
implementation/header through the public API. Automate verification where useful.

## Nift minification

**Status:** SETTLED

Opt-in by extension and applied after rendering at the final-output boundary.

## Malformed input

**Status:** PARTLY FORMAT-SPECIFIC / REVIEW CURRENT TESTS

Behavior must be controlled and documented per format. Do not accidentally turn
best effort, rejection, or conservative preservation into a universal policy.

## Fuzzing and broader corpora

**Status:** FUTURE/ONGOING HIGH-VALUE WORK

Add when it improves semantic confidence; not permission to block all useful
development until exhaustive language validation exists.

