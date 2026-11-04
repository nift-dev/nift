/* Tooling checkpoint (component 3, docs/runtime-2/checkpoints/tooling.md):
 * force-included (`-include src/build/production_tu.h`, PRODUCTION_GUARD in
 * the Makefile) first into every production translation unit: every object
 * linked into nift, libnift_c, the sanitizer and TSan nift builds, and the
 * runtime object sets that test binaries link. A test translation unit (a
 * file under tests/) is never compiled with it.
 *
 * It only marks the unit. rt::Storage (src/rt/aggregate.h) befriends the
 * storage-level unit tests' accessor StorageTestAccess, which only
 * tests/rt/storage_access.h defines; in a marked unit aggregate.h ends by
 * asserting that no definition of that name came before it and then poisons
 * the name, so nothing after it can name the accessor, however spelt, and
 * tests/rt/storage_access.h refuses to compile. It defines a macro and nothing
 * else, so units that do not include aggregate.h compile to the same object. */
#ifndef NIFT_PRODUCTION_TU
#define NIFT_PRODUCTION_TU 1
#endif
