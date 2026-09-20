# CP4 AST semantic inventory

Expression families to preserve: literals/null, live bindings, unary operators, arithmetic, comparisons/equality, logical short circuit, null coalescing, ranges, calls/lambdas, arrays/objects/collections, index/member/dynamic access, safe access, assignment/compound assignment and pre/post increment/decrement.

Statement/control families: declarations (`:=`), rebinding (`=`), expression statements, blocks/scopes, if/else-if/else, while, for/ranges/collections, functions/fragments, return/break/continue, structs/enums, import/export and script/template boundaries.

Current primary implementation entry points are `Parser::evaluate_expression`, `Parser::evaluate_condition`, `Parser::translate_function_program`, `Parser::execute_native_program` and `Parser::parse`. Dynamic/native/module/struct/collection call forms remain eligible for legacy fallback during the prototype. AST nodes cache syntax only; live values remain in existing `VariableBinding`/runtime storage.
