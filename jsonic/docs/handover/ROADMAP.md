# Jsonic++ roadmap

Near-term evidence priorities are established JSON conformance corpora,
coverage-guided fuzzing, repeated sanitizer/leak campaigns, differential parser
comparisons, and API/package usability checks. Feature growth is intentionally
secondary to proving the existing JSON contract.

The first general-purpose embedding extension is complete: strict parsing stays
the default, while comments, trailing commas, duplicate-key rejection,
structured locations and a caller-selected depth limit are available through
explicit options. Next integration work should exercise that API in a real
configuration consumer before considering wider syntax profiles or per-node
source spans.
