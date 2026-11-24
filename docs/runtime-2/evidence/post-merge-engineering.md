# Runtime 2.0 post-merge engineering

The release correction is under D-180. D-181 closes the 97 filesystem
recipes with 59 VM execution matches, 38 verified refusals, and 97 explicit
legacy matches. The original 102-row oracle remains sealed.

## Downstream work

| Root cause | Evidence and disposition |
| --- | --- |
| Free-call diagnostics | The native-dispatch gate passes 15 cases. The dead-code admission gate passes all 63 controls. |
| Growing a captured loop source | Direct array append, read-only lambda calls, and single-selector array sorting now pass the relevant growth cases. Clear, replacement, and mutating callable controls remain conservative. |
| Binding precedence | A bare read of the colliding name can resolve to the named function in legacy, while a call can resolve to the local binding. The VM's lexical lambda parameter result differs from the historical oracle. D-009 addresses built-in precedence. Keep the collision cases open. |
| Reserved declarations | The VM accepts `first` as a named-function parameter and as a receiver method. Ordinary declarations use the reserved-name gate. No approval extends the parameter exception to declarations. |
| Computed object targets | The historical prepared loop accepts a computed string key. A proof must also cover fallback to textual execution, evaluation order, and errors before widening the VM form. |
| Callbacks and module linking | Sort-factory still fails on module ownership, a nested returned factory, and a module member diagnostic. The callback overlay also retains the reserved-declaration failure. D-063 continues to govern keyed collection callbacks. |

## Hosted evidence

Historical run 45387812259 passed Linux and Windows. macOS passed the normal
terminal scenario and failed mutant 4's observation. The ownership audit
correction is in 9ebecaf0. The next run, 45416327694, failed earlier on macOS
when the shell smoke missed the continuation after `false`. The revised
fixture supplies an owned command, checks its marker and checks continuation.

FreeBSD run 45328750459 passed 48 exact-output comparisons and 86 applicable
VM checks. The original intermittent `open-2` omission remains without a
confirmed root cause. Preserve the mandatory exact-output assertion.
