# Canonical Jsonic++ duplicate-key investigation — design only

Canonical source: `jsonic/jsonic` at a956c50a4ba0092080902c19aa14132e7a885eb0.
The standalone header and Nift's vendored header have identical parsing bodies;
the standalone version constant is an existing difference. Neither was changed.

Strict duplicate rejection scans all preceding ordered members before accepting
each decoded key. Unique objects therefore require N(N−1)/2 comparisons.
At widths 8/32/128/512/2000/8000, strict parsing takes
10,114 / 48,308 / 341,333 / 4,355,260 / 44,506,194 / 1,030,717,909
instructions. Preserve mode takes 8,926 / 36,010 / 142,209 / 572,500 /
2,270,806 / 9,175,144. Both modes allocate the same number of blocks:
17 / 67 / 261 / 1031 / 4009 / 16011. The 8000-member strict object performs
31,996,000 comparisons. This is a separate quadratic construction cost; a
one-fetch runtime read does not remove it.

The retained 36-probe matrix includes unique Preserve/Reject, early/middle/end
duplicates, and malformed objects at every width. It retains exact error offsets
and messages. Duplicate detection currently precedes the colon and value parse;
that precedence is part of the proposed compatibility contract.

Proposed next checkpoint: a parse-local membership structure only in Reject
mode, activated above a measured small-object threshold. Keep Document's ordered
member vector and public layout intact. A table of hashes and member ordinals
can re-read `result.object[index].first` after vector relocation; no cached
string views or pointers into movable strings. Collision resolution must compare
complete decoded keys, including escaped Unicode-equivalent names. An owned-key
unordered set is simpler but adds key copies, nodes and buckets; measure its
allocation and RSS cost before choosing it. An ordinal open-address table at a
bounded load factor plausibly costs tens of bytes per member; this is a design
estimate, not an implemented measurement.

Require exact first-duplicate/error-position parity, duplicate-plus-malformed
precedence, escaped-key equality, recursive object memory bounds, Preserve mode
neutrality, small/scalar/array controls, standalone conformance and sanitizers.
Only after standalone acceptance should a deliberate vendored synchronization
be proposed. No index, parser implementation, or synchronization has started.

Additional retained exact precedence probes: escaped `a`/`\u0061` duplicate
reports offset 15; duplicate before bad colon or bad value reports offset 10;
a malformed first value reports offset 5 before reaching the later duplicate.
These preserve observable detection order for any future index design.
