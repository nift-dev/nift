# Jsonic++ testing

Use acceptance/rejection pairs, round trips, adversarial malformed inputs, sanitizer builds, external conformance corpora when available, and fuzzing. A parser accepting invalid JSON is as important a bug as rejecting valid JSON.

The independent `jsonic-cc/jsonic-conformance` repository is the external
release gate. It combines the complete pinned JSONTestSuite parsing and
transform corpora with project-owned semantic, API, generated, UTF-8 and
resource-stress cases. Parser changes should retain 810/810 mandatory passes.
