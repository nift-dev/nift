# Benchmark-focused optimizer checkpoint 11

The retained allocator orders ordinary one-character candidates by reference
frequency and uses source-character frequency when a function needs more than
the 54 one-character identifier names. A trial which counted duplicate
declaration spellings as additional uses did not change raw size and regressed
gzip size on three of four sampled bundles (Terser +14 bytes, Ant Design +58,
TypeScript +66; D3 improved by one byte). It was therefore rejected.

This checkpoint deliberately records the negative result and leaves generated
code unchanged. Name allocation remains deterministic and interference-aware.

