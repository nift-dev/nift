# Nift Architectural Rules

These rules are a design-review checklist for the stripped Nift rewrite. They are not a feature freeze; a future change can challenge one, but the trade-off should be explicit.

1. **Nift owns build-time composition, not the browser runtime.** Generate what is knowable at build time; leave genuinely runtime behavior to browser JavaScript, framework islands, backends or serverless functions.
2. **Ordinary web technologies remain ordinary.** HTML, CSS and JavaScript should not require Nift-specific wrappers merely to participate in a Nift project.
3. **Nift may perform optional final-output optimisation, but source-language compilation remains external.** Minifying final JS/CSS/HTML fits; becoming the TypeScript/Sass/JSX compiler does not.
4. **The template language is a build language, not a general scripting language.** Rendering-oriented loops, conditions and structured data fit; arbitrary mutable programming and embedded shell execution face a much higher bar.
5. **Structured data flows into rendering; Nift is not a general query/transformation engine.** JSON loading, schema validation, iteration, conditions and rendering-oriented sorting fit. Large joins/grouping/ETL pipelines belong in normal programs that produce Nift's input data.
6. **Compose with the wider ecosystem instead of recreating it through plugins.** Files, HTTP, standard CLIs and build scripts are first-class integration boundaries.
7. **Build orchestration belongs outside templates.** Use Make, npm/Bun scripts, shell scripts, CI, task runners or infrastructure tools instead of arbitrary command execution from Nift markup.
8. **Dependencies must be explicit enough to explain rebuilds.** A dependency means its state can affect output bytes; a requirement means the output assumes a path continues to exist.
9. **Incremental correctness outranks incremental cleverness.** A conservative extra rebuild is preferable to incorrectly trusting stale output.
10. **Failed builds should preserve the last known-good result.** Validate/render/minify before committing output and build metadata where practical.
11. **Safe operations are the default; destructive behavior is explicit.** For example, `nift minify app.js` writes `app.min.js`; `--in-place` is required to overwrite `app.js`.
12. **Project paths and derived outputs are correctness/security boundaries.** Tracked names and mutations must not escape permitted roots or collide silently.
13. **Concurrency may improve speed but must not change semantics.** Serial and parallel builds should be observationally equivalent.
14. **Diagnostics are part of the architecture.** Preserve tracked item, source file, line, column and useful context whenever Nift knows them.
15. **Configuration expresses policy; Nift should avoid hidden technology detection.** A `.ts` file does not silently invoke a compiler and a `package.json` does not silently run npm.
16. **Reusable mechanisms should have clean extraction boundaries.** If a subsystem is broadly useful, Nift may embed it through a small public API while keeping the subsystem independent of Nift project internals. The minifier follows this rule.
17. **Compatibility may be quiet, but legacy concepts should not shape the modern API.** Compatibility exists to reduce migration pain rather than to keep obsolete concepts permanently prominent.
18. **Every permanent feature pays a permanent complexity tax.** A feature should solve a recurring problem and remain explainable/testable without disproportionately expanding Nift's mental model.
19. **Tests protect contracts, not implementation accidents.** Correct a test when its assumption conflicts with the intended behavior instead of freezing an accidental implementation forever.
20. **The architecture should remain explainable in a compact flow.** Tracked item + content + template + structured build-time data → dependency-aware composition → optional final-output optimisation → ordinary files.

A useful final review question is:

> Does this make Nift better at the small build-time job it owns, or does it make Nift responsible for a neighbouring tool's entire problem domain?
