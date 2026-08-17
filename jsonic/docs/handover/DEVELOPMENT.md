# Jsonic++ development workflow

Start from a known-green standalone parser. State the behavioral guarantee being changed, add valid and invalid neighbors, implement the smallest change, run smoke/adversarial/sanitizer evidence, then synchronize Nift and Minify++ and run their integration gates. Do not optimize benchmark or fuzz results by weakening grammar/error contracts.
