# CP18 R2: exception-safe COW cloning

Parent: `6abe9971d6877b737acdce07c4a51f4c6d8c3d6a`, tree
`c2d5bc4ee7155099edcb12fd4ccbcb829deabc15`. R1 and its independent evidence
remain untouched. This correction lives in a separate worktree.

The independent reviewer demonstrated a leak in an admitted array-argument
mutation. `unshare` allocated a raw clone before copying its vectors, and before
checked-reference relocation of the original. Throwing at either point orphaned
the clone. Object and struct clone branches had the same ownership pattern.

A local `unique_ptr<HeapObject, CloneDeleter>` now owns each completed empty
clone immediately after allocation. The deleter invokes the existing noexcept,
iterative `destroy_heap_object`, which destroys the dynamic aggregate kind and
releases any copied children without requiring an allocation. The clone is
private, has exactly one owned reference, and is never exposed before commit.
Copies, identity-summary propagation, checked-reference relocation and heap
retally retain their original order. Only after all throwing work succeeds does
`release()` transfer ownership through the original noexcept `replace_heap`.
There are no added allocations or retain operations. On failure, the partial
clone and its copied children are released, its untallied footprint stays zero,
and the original value contents and ownership remain intact. `bad_alloc` still
propagates to the established C ABI INTERNAL containment boundary.

The unique-refcount return remains before the guard. The local GCC 15.2 -O2
assembly returns on the unique path before register-saving/stack setup, with no
guard construction or additional call there. VM dispatch and call-entry sources
are unchanged. This is an overhead audit, not a new performance campaign or
cross-platform claim. The existing make_array correction is byte-identical.

## Reachability and focused coverage

The accepted VM admits the reviewer's array program:

```
fn(g(a)) { a.push(1); return a.length(); }
b := [5];
print(g(b));
print(b.length());
```

It prints `2\n1\n`. The permanent ordinary allocation counter exhausts every
compile/run allocation index for this program, requires zero extra allocations
after each cleanup and program destruction, checks exact recovered output, and
runs the same compiled program successfully after every injected run failure.
Heap accounting is checked independently of malloc/new accounting: an untallied
orphan cannot hide behind a green VM heap counter.

All three clone branches additionally have direct aggregate tests. Fixtures
contain non-inline strings and an identity-summary marker; objects also have a
hash index. At every allocation site, checks cover unchanged shared contents and
identity after failure, correct heap/new accounting, successful retry, independent
mutation, struct type and identity summary, final cleanup, and the nonallocating
unique path. Checked builds also exhaust relocation allocations.

Array COW reachability is established through the admitted program above.
Object/struct COW correction coverage here is direct-unit coverage, without
changing admission. Representative object parameter/index and alias-copy forms
remain refused (`static-error:member-assign`, `index-target`, `aliasing`). Struct
copy/argument forms remain refused (`struct-value`, `aliasing`): the existing
compiler_structs.cpp contract admits a single holder and passes receiver slots,
not copied instances. An admitted unique-struct method still prints `2\n2\n`.
These witnesses describe tested boundaries; they are not a new blanket claim
that every conceivable object program is unreachable. No admission is lifted.

The R1 unsafe unshare function is frozen as a test fixture. Ordinary and
sanitized controls restore only that function, compile successfully and must
fail for real outstanding allocations / an unshare LeakSanitizer trace. The
make_array controls remain separate and unchanged. The ordinary COW gate is
reachable through test-runtime2-cp18; sanitizers remain through test-embed.
Certification removal controls pin the new edge.

## Scope and remaining obligations

Only aggregate.cpp changes in production. Public ABI 1.4 and 1.3 interfaces,
Python sources, source-copying, prevalidation, affinity and program/result
lifetime contracts are identical to R1. No CP17, CP14B method, Frame, Fault,
Handler, unwind, suspension or allocation-system changes are included.
Prior independent interface/packaging findings remain applicable by source
identity. The version-test decision is closed. R2 needs focused independent
confirmation before integration; full combined-tree and cross-platform
certification remain separate. No integration, push, CI, tag, release or
infrastructure spending is authorized.
