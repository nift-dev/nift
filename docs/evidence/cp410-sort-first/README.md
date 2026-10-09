# v4.10 sort-first checkpoint

Accepted/pushed source and all hosted certifications: `4d80ca47e94beb68544904e736a9f3415b303157`.
New runtime work is the separate, uncommitted one-fetch object experiment.
Sort architecture and canonical Jsonic++ work are proposals only.

See [callable architecture](callable-architecture.md), [Jsonic++ proposal](jsonic-proposal.md),
[accepted hosted results](accepted-publication.json), and [frozen relocation proof](frozen-relocation-audit.json).
The final report records measurement and safety decisions.

Harnesses retain the actual diagnostic build commands, instrumentation and probe sources.
For sort measurement, build on accepted source `4d80ca47`, before the object patch;
use that production binary and the accepted-state diagnostic objects. Do not link
phase objects against the one-fetch candidate. Paths and filenames have fixed
length to control source-location allocation costs. Phase scopes overlap.

The original scratch paths in harnesses identify the run's provenance; they are
not generic test entry points. Maintained `make test-v410-*` targets are the
reproducible semantic/unit/scan guards. CPU timings are local alternating paired
process measurements and never CI timing thresholds. No official benchmark run
or external peer ratio is claimed.
