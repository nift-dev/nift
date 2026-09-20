# CP38 — string concatenation investigation

The campaign's existing direct `string +=` scaling evidence remains super-linear at large sizes (20k ~408 ms, 40k ~708 ms, 80k ~1.51 s, 160k ~3.71 s, 320k ~10.2 s). The earlier attempted in-place append did not remove the dominant copies because Nift's current by-value `json::Document`/Value flow still materializes/copies the growing string.

CP17 also demonstrated that compound assignment's *expression-reparse* cost can be removed independently by prepared AST execution. Those are separate costs: prepared execution removes repeated source interpretation, while repeated growth of a by-value string can still copy O(total output) bytes.

Decision for this checkpoint: do not introduce rope/COW/shared mutable strings during the AST migration. That would alter the value/aliasing model and combine two architectural changes. DeepSeek should remeasure `string +=` after prepared compound-assignment coverage is complete; only the residual copy/allocation curve should drive a representation change. `array.push(...).join("")` remains the efficient construction strategy for intentionally large concatenations in the meantime.
