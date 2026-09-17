# `nift eval` exit contract

Stable classes reserved by CP115: 0 success; 2 expression/usage failure; 3 input/file failure; 4 missing/invalid project context; 5 schema/content-model failure. CP128 freezes these numbers. Current expression-only evaluation primarily emits 0/2; later project/schema hosts must use 4/5 rather than collapsing those failures into prose-only status.

stdout is result-only. Diagnostics go to stderr. `--json` never emits ANSI/presentation text.
