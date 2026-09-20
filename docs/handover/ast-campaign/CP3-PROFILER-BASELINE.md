# CP3 profiler baseline freeze

The pre-AST Callgrind campaign found a common hot signature across loops/arithmetic/BFS dominated by repeated source-string work: string construction/append/copy, `rfind`/operator scanning, literal/type classification and recursive `evaluate_expression` dispatch. The working estimate was roughly 60% of representative instruction cost in string/expression machinery.

The AST experiment's profiler success criterion is not merely lower wall time: this signature must materially shrink. CP17 re-profiles the prototype; the current checkpoint freezes the existing evidence and target.
