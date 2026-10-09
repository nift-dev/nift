# v4.10.0 release-note accuracy audit

Reviewed the complete Git delta from immutable **v4.9.0**
(`1314a4b169ea2588df8bf63a3f342639ea7b5aa7`) through the release candidate.
Preparation changes after the reviewed runtime are documentation/evidence only.

Canonical workflow path is
`docs/evidence/release-4.10.0/release-notes-4.10.0.md`, verified by the `rehearse`
job of `.github/workflows/release.yml` (present and tracked). The reviewed notes
are substantive release notes, not placeholder evidence.

| User-facing statement | Source / history cross-check |
|---|---|
| Tracked-item `pre-build`/`build`/`post-build`/`depends` fields | `4bff682`; `src/Types.h` `ItemBuildPipeline`; `src/ProjectRead.cpp` parsing/validation; `docs/BUILD-SCRIPTS.md`. |
| Dotted canonical sidecars; deprecated hyphenated pre/post fallback; ambiguity rejected | `src/ProjectRead.cpp` sidecar discovery; `tests/v410_build_pipeline.py`; black-box NRS `v410_build_pipeline_smoke.sh`. |
| Custom `build` replaces render/minify and owns output; missing output fails | `src/ProjectInfo.cpp` phase ordering and bypass; `docs/BUILD-SCRIPTS.md`. |
| Metadata committed only after post; failed phase stops item; branches continue | `src/ProjectInfo.cpp`; independent branch completion and descendant blocking. |
| `depends` validation, cycle path, closure, clean-prerequisite validation | `src/ProjectRead.cpp`, `src/ProjectInfo.cpp`; `tests/v410_build_pipeline.py`. |
| `depends` orders completion, does not invalidate; ordinary deps still needed | `build_reasons` excludes `depends`; documented relationship to `@dep`/`.deps.json`. |
| Parallel ready-queue, no polling, no-dependency fast path | `src/ProjectInfo.cpp` condition-variable scheduler and atomic-index path. |
| Hook environment variables and project-root resolution | `src/ProjectInfo.cpp` `build_environment`; `docs/BUILD-SCRIPTS.md` table. |
| Additive existing-project rewrite/redesign | `4bff682`; `src/CLI.cpp` transformation init; `ReleaseNotes.md` v4.10 section. |
| Capture/callback allocation reduction | `57d3cb1`, `561cfe8`; `Parser.cpp`/`Parser.h` shared captures and overlay frames. |
| Object-member traversal reduction | `7dfd2eb`; `RuntimeValue::find_member` and hot-path rewires. |
| Recursive glob/traversal sharing and linear replace | `391dac5`, `34dfe75`, `e3ef89d`; `ParserHelpers.*`, `ParserExpression.cpp`. |
| Prepared filesystem operations | `615ffcd`; prepared stat/copy/move recipes. |
| Large-string preparation | `e290b85`; bounded canonical large-string expressions. |
| FileValue/save ownership reductions | `8ad6632`; prepared FileValue operations and shared clean saved buffers. |
| Windows alias-cache correction | `3d8a8e9`; native alias resolution for build cache invalidation. |
| Portable C++17 shared ownership | `2eaec7d`; `src/Types.h` replaces `shared_ptr::unique()`. |
| No new official benchmark result; frozen series unchanged | `PERFORMANCE.md`; `docs/handover/ROADMAP.md`; official series not rerun. |
| ABI 1.3 and no removed/deprecated syntax | `include/nift/c_abi.h`; `C ABI remains 1.3`; no removal in the delta. |
| Deferred work (sort/capture/frame/instance, indexes, VM/JIT, multiple outputs, Make/Ninja comparison) | `docs/handover/V4.10-BUILD-PIPELINE.md` "Deferred" sections; `PERFORMANCE.md`. |

Website reconciliation (release-version JSON-LD, additive transformation wording,
published-not-current benchmark wording) was published and byte-verified live; no
runtime claim changed.

The cleanup-harness fix shipped in the independent regression suite
(`nift-regression-suite`) is test-harness hygiene and is not a Nift runtime change;
it is recorded as a **fixed non-runtime release-readiness issue**.
