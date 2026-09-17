# CP187 hierarchy semantics contract

This fixture freezes the intrinsic project-page hierarchy public semantics
before CP188+ implementation.

## Page identity

- A **page** is a tracked item, identified by its tracked `name` (for example
  `/`, `docs`, `docs/advanced`). The canonical identity of a page is its exact
  tracked name string.
- Page names are already validated by Nift's tracked-name/path rules; the
  hierarchy sees only valid tracked names (no `..`, no empty segments, no
  leading/trailing slashes other than the root `/`).
- A **page value** is an opaque reference to one tracked page
  (`page("name")` or the current-page binding `page` in a page template).

## Parent derivation

- A page's **parent** is the nearest *existing* tracked ancestor by name path.
  - `docs/advanced/guide`'s parent is `docs/advanced` if that page exists;
    otherwise `docs` if that exists; otherwise `/` (the root).
  - A top-level page such as `about` has parent `/`.
  - The root page `/` has **no parent** (its `parent` is `null`).
- If no tracked ancestor exists and `/` is not tracked, a page has no parent
  (`null`). (The root `/` is normally tracked as the site home page.)
- Missing/intermediate parents are never materialized as pages; they are
  transparent: a page attaches to its nearest existing tracked ancestor.

## Child ordering

- A page's **children** are the tracked pages whose nearest existing ancestor
  is that page, ordered by **tracked order** (the order they appear in
  `.nift/tracked.json` / the tracked index). This is deterministic and stable.

## Root / top-level / nested

- `/` is the single root when tracked. Top-level pages are its children.
- Nested pages (`docs/advanced/guide`) are children of their nearest existing
  ancestor.

## Self inclusion / exclusion

- `ancestors` and `descendants` **exclude** the page itself. `ancestors` are returned top-down (root first, immediate parent last) — the natural breadcrumb order; `descendants` are returned in pre-order.
- `siblings` **excludes** the page itself (if siblings is accepted).

## Siblings

- `siblings` is **ACCEPTED** as an intrinsic hierarchy relation.
- Siblings of a page = the other children of its parent (excluding the page
  itself), in the parent's child order (tracked order).
- A root page (no parent) has no siblings (`null`). A page whose parent has no
  other children has no siblings (`null` or an empty array — empty array).

## Cycles / malformed structure

- Cycles are impossible by construction: a parent is always a strict path
  prefix of its child, so parentage is acyclic.
- Extreme depth and width are handled without pathological behavior.

## previous / next

- `previous` and `next` ordered navigation are **DEFERRED** (they require an
  explicit ordering context and are not intrinsic hierarchy).

## Composition

- Page values compose with the rest of the language:
  - `page.parent`, `page.children`, `page.ancestors`, `page.descendants`,
    `page.siblings` on any page value.
  - Data fields on a page value: `name`, `title`, `path`, `output_path`,
    `url`, `metadata`, `content`, `type`.
  - Collection/postfix composition: `page.children.filter(...)`,
    `page.ancestors.map(...).join(",")`, `page.parent.title`,
    `page.children[0].url`, `page(name).children.size()`.
  - Template (`$[...]`/`@for`), `nift run`/`nift sh`, and `nift eval` share
    the same page/hierarchy machinery (parity).

## Obtaining a page value

- `page` (bare) in a page template binds to the **current page**.
- `page(name)` returns the page value for a tracked name in templates,
  scripts and `nift eval`.
- `page` (bare) outside a page context (for example `nift eval`) has no
  current page; use `page(name)`.

## Pay-for-use

- The hierarchy index is constructed lazily on the first hierarchy query and
  reused; projects that never query hierarchy construct no index and no
  hierarchy state.