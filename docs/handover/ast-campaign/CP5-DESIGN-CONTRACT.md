# CP5 AST design contract

The prototype uses immutable syntax nodes owned by `std::unique_ptr`, with byte-offset source spans. Evaluation is deliberately separated from parsing. Bindings are represented by names in the tree and resolved against the current Parser scopes at execution time; values are never captured by parsing. Unsupported syntax returns a clean `unsupported` result and falls back to the legacy evaluator.

Control statements use an explicit execution result (`normal`, `break`, `continue`, `return`, `error`) rather than textual sentinel handling. No bytecode, JIT, slot-resolution semantic change, closure redesign or Value redesign is authorized before CP17. The legacy evaluator remains the differential oracle.
