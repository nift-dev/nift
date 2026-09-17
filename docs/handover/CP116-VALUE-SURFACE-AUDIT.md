# CP116 value surface audit

Current expression runtime is centralized in `Parser::evaluate_expression`; `$[...]`, native programs and collection callbacks already converge there. Existing scalar methods cover strings/numbers and arrays cover size/empty/first/last/contains/join/slice plus mutable array/collection transformations. Existing global JSON collection helpers overlap with native array methods.

Gaps accepted for CP117-CP124: ordinary JSON-object introspection/default/composition; array `find_index`, `sort_by`, `unique`, `flatten`; direct `sum/min/max`; `group_by`; and a chained/adversarial parity wall. These are implemented in the expression runtime, not `Value`'s C++ wrapper, so all language hosts receive identical semantics.
