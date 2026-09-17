# CP115 structured-data/content contract

Accepted public direction for the CP115-CP167 campaign:

- value operations use method syntax (`obj.keys()`, `array.sort_by(fn)`);
- one expression engine backs templates, scripts and `nift eval`;
- `nift eval --json` writes only JSON to stdout and diagnostics to stderr;
- `.schema` and `.tax` are the project definition extensions;
- `.nift/config.json` explicitly lists schema/taxonomy sources;
- conventional `---` YAML front matter is supported;
- `.nift/tracked.json` may instead name one external `frontmatter` source;
- inline plus external front matter is a hard error and is never merged;
- tracked/front-matter `type` values, when both present, must agree;
- project information is read-only and available through `project` in builds and eval;
- provisional discovery names remain `project.content.<type>`, `project.schemas.<name>` and `project.taxonomies.<name>` until dogfood;
- no magic page generation, directory scanning, arbitrary mutation, or jq-compatible second language is introduced.

Evaluator exit classes are frozen as: 0 success, 2 parse/expression error, 3 input/file error, 4 project-context error, 5 schema/content-model error. Later checkpoints may refine diagnostics without reusing these classes for unrelated failures.
