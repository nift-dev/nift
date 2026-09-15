# Nift v4.1 CP24 certification

The v4.1 language-focused wall passes at the CP24 endpoint: template variables, cross-feature language composition, operator hardening, injected dependency rebuilding, existing control-flow behavior, and JSON Schema integration.

## Rebinding representation

`VariableBinding::value` is a `std::shared_ptr<json::Document>`. Assignment replaces the binding's shared handle; it no longer overwrites the old `Document` in place. Direct-variable loops can therefore retain the original collection object when the source binding is rebound. Jsonic++ `Document` is itself value-owned, so structured expression materialisation can still copy; the O(1) claim applies specifically to binding-handle replacement.

During CP24, expression-source injection exposed a scope-vector lifetime bug in declaration: the declaration path held a reference to `variable_scopes_.back()` while evaluating its RHS; an injected child scope could reallocate the vector and invalidate that reference. The declaration now reacquires the current scope after RHS evaluation. The cross-feature wall covers this path.

## Zero-mutation scenario 11

Scenario 11 is not a v4.1 regression. The identical three failures reproduce at:

- released/pre-campaign v4.0.13 baseline `cdb103f`;
- CP9 `f353982`;
- the CP24 endpoint.

The failures are: failed-reconcile unexpectedly returns success, the unfinished marker is absent after a derived deletion, and a following ordinary build therefore does not refuse. `build --repair` still recovers. This is a genuine pre-existing build/reconciliation defect and remains outside the template-language campaign; it must not be presented as introduced by v4.1.

## Remaining environment gate

The aggregate `make test` reaches `test-pagination-ordering` and stops because `strace` is unavailable in this execution environment; that test deliberately fails closed with exit 77 when its ordering guarantee cannot be observed. The v4.1-focused gates listed above are green. CP24 records this limitation rather than claiming that unavailable gate passed.
