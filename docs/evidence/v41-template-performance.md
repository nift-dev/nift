# Nift v4.1 template-language performance evidence

CP22 audits binding representation as well as wall-clock behavior.

`VariableBinding::value` is a `std::shared_ptr<json::Document>`. Assignment now replaces that handle rather than overwriting the old `Document`, so an active direct-variable loop can retain the original collection identity after the source name is rebound. The handle replacement itself is O(1).

Jsonic++ `Document` is value-owned (`std::string`, `std::vector<Document>` arrays and object member vectors). Therefore this is **not** a claim that all structured expression evaluation is O(1): materialising an RHS/result can copy a structured `Document`. The important correction is that assignment no longer deep-mutates the object already referenced by aliases/active iterators. Future performance work may remove avoidable result copies without changing the language semantics.

The v4.1 feature checks are opt-in; ordinary templates retain the existing parser path except for bounded directive/expression dispatch checks. CP24 remains the release certification gate.
