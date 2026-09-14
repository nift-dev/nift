# Mangler checkpoint 7: exceptional bindings and named-binding boundary

Recursive catch bindings and class static-block variable scopes are now
modelled and tested. Local function/class-name mangling was also prototyped,
but was not retained: although it produced very large raw-size reductions, the
TypeScript fixture failed execution because the lightweight resolver could not
yet prove the identity of function declarations also stored on the public `ts`
object (`createBinaryExpressionTrampoline`).

Named function and class bindings therefore remain a deliberate fallback until
stored/exported identity is represented directly. Method parameters, catch
patterns and static-block locals remain enabled because the complete generated
corpus and real-bundle validator accept them.

