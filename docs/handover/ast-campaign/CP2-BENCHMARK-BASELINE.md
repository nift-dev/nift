# CP2 benchmark baseline freeze

The AST campaign preserves the current scripting-benchmark raw results as the external baseline. Key post-v4.4-optimization large-workload observations entering the campaign are loops ~1.5s in focused reruns, arithmetic ~1.5s, function calls ~2.4s, Fibonacci ~3.4s, BFS ~13.9s, and fair pre-generated JSON inject parse ~0.33-0.35s. Historical aggregates with different JSON construction semantics must not be merged with the fair parse workload.

CP17 will compare the AST prototype against a freshly rebuilt legacy/current binary on the same machine; DeepSeek is explicitly tasked with independently checking those numbers against the preserved scripting-benchmark baseline.
