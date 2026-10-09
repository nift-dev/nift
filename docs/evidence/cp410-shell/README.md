# Expanded shell performance investigation

See [the completed review](report.md). The frozen official series is unchanged. The final glob-only candidate is local, uncommitted and unpushed; full local checks pass, while its new hosted cross-platform certificate awaits review/publication. All ten accepted-baseline hosted walls are green. The dispatch experiment was rejected.

Final JSON receipts identify ordinary production binaries. `dispatch-experiment-*` receipts preserve the rejected candidate; initial traversal-baseline timing and exploratory write timing are explicitly excluded from acceptance conclusions. `sources` contains fixtures/generators/controls, `profiles` and `raw-profiles.tar.gz` preserve diagnostics, and `safety` contains the final certificate.
