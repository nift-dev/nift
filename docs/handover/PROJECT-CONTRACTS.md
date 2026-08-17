# Project contracts

This is the living behavioral/implementation handover for Nift project contracts.
Keep it aligned with source, focused tests, the standalone regression suite, and
public website documentation.

## Purpose

Project contracts let a project expose checked project-wide JSON namespaces
without adding new template syntax. They are declared explicitly in
`.nift/config.json` and consumed through the existing `$[...]` value model.

Example:

```json
{
  "config": {
    "contracts": {
      "routes": ".nift/routes.json"
    }
  }
}
```

```nift
$[routes.users.list]
```

The feature was derived contract-first from a desired guarantee: a project should
be able to name shared relationships once and have Nift fail clearly when a
referenced declaration/source/member is no longer valid, while preserving normal
incremental dependency behavior.

## Behavioral contract

A configured contract namespace:

1. is a valid identifier and cannot collide with built-in metadata/reserved
   bindings;
2. maps to a non-empty project-relative path that must stay inside the project;
3. is loaded lazily only when its namespace is resolved;
4. is parsed as immutable JSON using the same build-wide shared JSON cache as
   `@json`;
5. uses the same member/index/scalar rendering semantics as explicit JSON
   bindings;
6. causes the output to depend on both the contract source and
   `.nift/config.json`;
7. cannot be shadowed/overloaded by `@json`, array-loop bindings, or object-loop
   bindings;
8. reports contract-specific missing-source, parse, and missing-entry failures;
9. does not cause Nift to reinterpret arbitrary unknown `$[...]` roots as
   contracts;
10. participates in control-flow and textual-parameter interpolation through the
    existing JSON value resolver.

Unused configured contracts are not parsed and do not become dependencies of
outputs that never reference them.

## Why config declaration rather than discovery or new syntax

Rejected approaches included `@route(...)`, `@routes(...)`, dynamic user-defined
`@contract` directives, automatic `.nift/contracts/*.json` discovery, reserved
magic `$[routes.*]` without declaration, and a new contract sigil. They either
made errors ambiguous, introduced parser/embedded-language collisions, required
more syntax than the guarantee justified, or generalized beyond demonstrated
need.

The config-declared approach reuses existing `$[...]` semantics and makes
contract intent explicit before lookup, so errors and namespace ownership remain
predictable.

## Routes are a pattern, not a built-in routing system

A contract named `routes` is a useful project convention:

```json
{"users":{"list":"/api/users"}}
```

```nift
fetch('$[routes.users.list]')
```

Nift route contracts are application/build-time declarations. They are not the
same as Vercel routing rules or another deployment platform's runtime request
routing. Platform adapters may eventually validate or derive provider-specific
configuration from project contracts only if that remains simple and reliable.

## Testing requirements

Tests are executable evidence for the contract itself, not an implementation
appendix. Preserve coverage for:

- successful scalar/member/index resolution;
- lazy loading and exclusion of unused contract files from dependency metadata;
- contract-source dependency invalidation;
- config-remapping invalidation and dependency replacement;
- use inside parameter interpolation, `@if`, and `@for`;
- malformed/missing source and missing member errors;
- arrays/objects rejected when rendered directly;
- local `@json` compatibility when no contract reserves the name;
- unknown-root behavior when neither a binding nor contract exists;
- `@json` and loop shadow/collision rejection;
- invalid/reserved contract names;
- malformed contract config values;
- lexical/symlink path escapes;
- focused source-tree + standalone black-box synchronization.

When a new bug family is found, add a permanent regression before treating the
checkpoint as restored.

## Design rule retained from this feature

Design guarantees before designing features. A guarantee is not enough merely
because it is theoretically verifiable: it must also be valuable and simple
enough to express, implement, diagnose, test, and explain. Before adding syntax,
check whether existing Nift primitives already satisfy the desired contract.
