// B4b 23 (docs/runtime-2/checkpoints/b4b-element-types.md section 15): the kind inference's
// analysis work on one script, as deterministic counts (UnitKinds::work), for the
// compile-scaling gate (tests/runtime2/b4b/work.py). The script is planned and inferred exactly
// as the compiler does for a script unit (plan_unit, plan_globals, infer_unit_kinds); the
// guard and code generation do not run.
//   b4b-work FILE...   one JSON line per file
// The same source is linked with B4a's inference objects to measure the B4a baseline
// (tests/runtime2/b4b/work_b4a.sh): `nodes` and `enqueues` are counted at the same points.
#include "rt/globals.h"
#include "rt/infer.h"
#include "rt/parser.h"
#include "rt/resolver.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace nift::rt;

int main(int argc, char** argv) {
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        std::ifstream in(argv[i]);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string src = ss.str();
        Ast ast(src);
        const ParseResult parsed = parse_script(ast);
        if (!in || !parsed.ok) {
            std::printf("{\"file\": \"%s\", \"error\": \"parse\"}\n", argv[i]);
            status = 1;
            continue;
        }
        const UnitPlan plan = plan_unit(ast, parsed.root);
        const GlobalPlan globals = plan_globals(ast, parsed.root, plan);
        const UnitKinds k = infer_unit_kinds(ast, parsed.root, plan, 0, &globals);
        std::printf("{\"file\": \"%s\", \"planned\": %d, \"converged\": %d, \"units\": %zu, \"walks\": %zu, \"nodes\": %zu, "
                    "\"enqueues\": %zu, \"materializations\": %zu, \"site_unions\": %zu, \"reader_moves\": %zu, "
                    "\"site_items\": %zu, \"unite_items\": %zu}\n",
                    argv[i], static_cast<int>(plan.ok), static_cast<int>(k.converged), plan.functions.size() + 1, k.walks, k.work.nodes,
                    k.work.enqueues, k.work.materializations, k.work.site_unions, k.work.reader_moves,
                    k.work.site_items, k.work.unite_items);
    }
    return status;
}
