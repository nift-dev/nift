# CP-F bounded measurement evidence

Runtime candidate and baseline identities are in `provenance.json`. Raw whole
process samples are in `overhead.json`; the final report explains aggregation.
The scripts were run from the Nift checkout with working files under `.build/cp-f`.
To reproduce, copy these profiling sources/scripts to `.build/cp-f`, reconstruct
(`git archive ab687d0 | tar -x -C .build/cp-f/baseline-src`)
and build exact ab687d0 at `.build/cp-f/baseline-src` with the same compiler/flags,
and build the candidate checkout. Run `python3 .build/cp-f/measure-overhead.py`.
This script creates six fixtures and takes balanced interleaved measurements.

`build-probe.py SOURCE OUTPUT [REPO]` links a probe against the selected built
checkout's CLI objects excluding entrypoint/C ABI objects. Build and run
`translation-classification.cpp` separately against baseline and candidate, using
the generated native fixture files and HTML fixture as arguments. This measures
translation plus structural classification, not pure translation or template
rendering. `source-map-metrics.cpp` requires the candidate's private test friend;
it prints logical mapping storage and line-index bytes for the same arguments.
The HTML sample deliberately exercises native classification of HTML-like input;
normal template identity views require no sparse mapping array.

Sanitizer JSON files record separate parser fuzz and core lifecycle checks.
Machine/compiler, flags, process startup, filesystem cache, and fixture size affect
results. RSS and wall-time changes are measured costs, not pass/fail thresholds.
