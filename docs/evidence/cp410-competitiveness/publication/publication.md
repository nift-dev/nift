# Accepted candidate publication and hosted certification

Runtime commit: `57d3cb115bb3c0bc61e57d74d15ab5483f154103`.
Guards/evidence commit and initial pushed SHA:
`0c22f5f7cad04410acabe7459b59a340c33309d4`.
Normal push succeeded; main was clean, zero ahead and zero behind.
Focused committed-state diagnostics, callable/callback parity, allocation guard,
119 bundled probes, 58 selector cases and 19 factory/capture cases passed.

Initial Deep Guards run #37851048382 failed the independent NRS version checks:
the workflow retained `NIFT_EXPECT_VERSION=4.9.0`, while this authorized development
candidate correctly reports `Nift v4.10.0`. The historical suite failed only its
version assertion (573/574 passed); the CLI version module failed the same mismatch.
The remaining NRS modules passed, as did every other Deep job including both
sanitizer jobs and PRS. See the preserved initial failure log.

The workflow expectation is corrected to 4.10.0. No runtime/test assertion or
independent contract pin changed. Exact-SHA hosted recertification is required;
this initial run is not represented as green or discarded as flaky.
