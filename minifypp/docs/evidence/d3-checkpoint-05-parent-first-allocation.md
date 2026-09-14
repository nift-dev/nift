# D3 checkpoint 5: parent-first coordinated allocation

Date: 2026-09-13

Function units are now allocated in lexical-depth order. This matters because
concise-arrow scopes are synthesized after brace-backed children and therefore
cannot safely use raw scope-vector order. Parent-first ordering makes each
coordinated outer name available when a child reserves captured names.

With that invariant in place, the emergency "more than one arrow" whole-unit
barrier was removed. The focused nested-arrow collision regression now safely
renames the captured outer binding and assigns distinct child-local names.

Retained D3 result:

| Mode | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Structured | 336,276 | 295,969 | -40,307 (-12.0%) |
| Aggressive | 332,821 | 292,532 | -40,289 (-12.1%) |
| Structured gzip | 98,752 | 94,344 | -4,408 (-4.5%) |
| Aggressive gzip | 98,310 | 93,952 | -4,358 (-4.4%) |

The standalone smoke suite, Node script/module semantics, 15,459 generated
programs, scope-adversarial suite, structured semantics, and aggressive
differential suite passed before retention. Full external conformance is the
next checkpoint.
