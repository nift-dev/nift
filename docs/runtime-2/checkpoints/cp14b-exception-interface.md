# CP14B exception interface seam

This commit contains only the approved private boundary-drain declaration,
per-program flag and constructor initialization, and the five-line caught
unwind notification. It applies to the accepted CP13B base `70fda902`.

The cold hook receives the current frame and selected handler depth, runs
before destructive unwinding, and reports logical failure as terminal Internal.
It is never called on terminal paths. Its implementation and collection
metadata belong to subsequent CP14B files. No Error/reporting semantics change.

Programs without comparator boundaries pay one predictable caught-throw branch.
The hook implementation is supplied by the complete CP14B candidate; this
interface commit by itself is not an independently linkable candidate.
