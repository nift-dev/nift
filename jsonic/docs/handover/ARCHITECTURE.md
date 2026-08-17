# Jsonic++ architecture

Jsonic++ deliberately keeps one implementation header. `json::Document` owns JSON values as null/boolean/number/string/array/object and includes the parser/serializer. The small shape is intentional: easy vendoring and auditable behavior are part of the product contract.

The standalone project owns parser semantics; Nift and Minify++ own integration semantics around the parser.
