#!/usr/bin/env bash
set -euo pipefail
BIN=$(pwd)/nift; R=$(mktemp -d); trap 'rm -rf "$R"' EXIT
mkdir -p "$R/.nift" "$R/content/recipes" "$R/templates" "$R/model" "$R/public"
cat > "$R/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified","schemas":["model/recipes.schema"],"taxonomies":["model/recipes.tax"]}}
JSON
cat > "$R/model/recipes.tax" <<'EOF2'
@taxonomy(cuisine)
@taxonomy(diet)
EOF2
cat > "$R/model/recipes.schema" <<'EOF2'
@schema(recipe) {
title: string
cuisine: taxonomy(cuisine)
diet: taxonomy(diet)[]
featured: bool = false
}
EOF2
cat > "$R/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"recipes/risotto","title":"Risotto","type":"recipe"},{"name":"recipes/curry","title":"Curry","type":"recipe"},{"name":"recipes/tacos","title":"Tacos","type":"recipe"}]}
JSON
cat > "$R/templates/template.html" <<'EOF2'
<h1>$[frontmatter.title]</h1>
@content
EOF2
cat > "$R/content/recipes/risotto.md" <<'EOF2'
---
title: Mushroom Risotto
cuisine: italian
diet:
  - vegetarian
featured: true
---
Cook the rice.
EOF2
cat > "$R/content/recipes/curry.md" <<'EOF2'
---
title: Thai Green Curry
cuisine: thai
diet:
  - gluten-free
---
Cook the curry.
EOF2
cat > "$R/content/recipes/tacos.md" <<'EOF2'
---
title: Bean Tacos
cuisine: mexican
diet:
  - vegetarian
  - gluten-free
---
Cook the beans.
EOF2
(cd "$R" && "$BIN" build --all >/dev/null)
[[ $(cd "$R" && "$BIN" eval --json '(project.content.recipe).size()') == 3 ]]
[[ $(cd "$R" && "$BIN" eval --json '(project.taxonomies.cuisine).size()') == 3 ]]
[[ $(cd "$R" && "$BIN" eval --json '(project.taxonomies.diet).size()') == 2 ]]
[[ $(cd "$R" && "$BIN" eval --json '(project.content["recipe"]).count(r => r.featured)') == 1 ]]
