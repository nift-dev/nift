CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -pthread
CPPFLAGS ?= -Isrc -Iinclude -Iminifypp/include -Iminifypp/src
LDFLAGS ?=
LDLIBS ?=

# Shared core + CLI implementation (the ordinary Nift CLI needs only these).
CORE_SOURCES := src/nift.cpp src/ProjectOwnership.cpp src/CLI.cpp src/Value.cpp src/FileSystem.cpp src/JsonFile.cpp src/JsonSchema.cpp minifypp/src/Minify.cpp src/Parser.cpp src/ProjectInfo.cpp src/ProjectRead.cpp src/ProjectState.cpp src/WatchList.cpp src/BuildProgress.cpp
# Embedding-exclusive implementation (Engine, Context, C ABI). The reduced CLI
# never compiles or links these; they are built by the embed library and the
# engine/C ABI test targets.
EMBED_SOURCES := src/embed/Engine.cpp src/embed/Context.cpp src/embed/c_abi.cpp
SOURCES := $(CORE_SOURCES) $(EMBED_SOURCES)
OBJECTS := $(SOURCES:.cpp=.o)
CLI_OBJECTS := $(CORE_SOURCES:.cpp=.o)
DEPFILES := $(OBJECTS:.o=.d)

ifeq ($(OS),Windows_NT)
	EXEEXT := .exe
	PREFIX ?= $(LOCALAPPDATA)/Programs/Nift
	INSTALL_PROGRAM = cp
	# Self-contained Windows binaries: the mingw runtime DLLs (libstdc++-6,
	# libgcc_s_seh-1, libwinpthread-1) are not guaranteed to be on consumer
	# PATH, so the CLI and every embedded consumer link the runtimes statically.
	LDFLAGS += -static -static-libgcc -static-libstdc++
	SHARED_LIB := libnift_c.so
else
	EXEEXT :=
	PREFIX ?= /usr/local
	INSTALL_PROGRAM = install -m 0755
	ifeq ($(shell uname -s),Darwin)
		# macOS shared library is a Mach-O .dylib with a relocatable install name.
		SHARED_LIB := libnift_c.dylib
	else
		SHARED_LIB := libnift_c.so
	endif
endif

TARGET := nift$(EXEEXT)
BINDIR ?= $(PREFIX)/bin
DESTDIR ?=

TEST_DIR := .build
SANITIZER_FLAGS ?= -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
SAN_TARGET := $(TEST_DIR)/nift-sanitize$(EXEEXT)
SAN_OBJECTS := $(patsubst %.cpp,$(TEST_DIR)/san/%.o,$(SOURCES))
TSAN_FLAGS ?= -O1 -g -fno-omit-frame-pointer -fsanitize=thread
TSAN_TARGET := $(TEST_DIR)/nift-tsan$(EXEEXT)
TSAN_OBJECTS := $(patsubst %.cpp,$(TEST_DIR)/tsan/%.o,$(SOURCES))
MEMORY_SMOKE := $(TEST_DIR)/nift-memory-san$(EXEEXT)
JSON_TEST := $(TEST_DIR)/nift-json-smoke$(EXEEXT)
JSON_SCHEMA_TEST := $(TEST_DIR)/nift-json-schema-smoke$(EXEEXT)
RECOVERY_EPOCH_GUARD := $(TEST_DIR)/nift-recovery-epoch-guard$(EXEEXT)

all: $(TARGET)

# The shipped CLI is the REDUCED object set (no src/embed/*). Plain `make`
# therefore builds only the ordinary Nift CLI; the embedding library and every
# language binding are explicit, optional targets.
$(TARGET): $(CLI_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(CLI_OBJECTS) $(LDLIBS) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPFILES) $(patsubst %.o,%.d,$(SAN_OBJECTS) $(TSAN_OBJECTS))

test-jsonic:
	$(MAKE) -C jsonic test

test-jsonic-sync:
	@test -n "$(JSONIC_DIR)" || (echo "JSONIC_DIR=/path/to/jsonic is required" >&2; exit 2)
	$(MAKE) -C "$(JSONIC_DIR)" check-nift-sync NIFT_DIR="$(CURDIR)"

test-json:
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/json_smoke.cpp -o "$(JSON_TEST)"
	"$(JSON_TEST)"

test-json-schema:
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/json_schema_smoke.cpp src/JsonSchema.cpp -o "$(JSON_SCHEMA_TEST)"
	"$(JSON_SCHEMA_TEST)"

$(TEST_DIR)/nift-console-smoke$(EXEEXT): tests/console_smoke.cpp src/Console.h
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/console_smoke.cpp -o "$@"

test-console: $(TEST_DIR)/nift-console-smoke$(EXEEXT)
	"$(TEST_DIR)/nift-console-smoke$(EXEEXT)"

PROGRESS_RENDER_TEST := $(TEST_DIR)/progress-render$(EXEEXT)
$(PROGRESS_RENDER_TEST): tests/progress_render_unit.cpp src/BuildProgress.cpp src/BuildProgress.h src/Console.h
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/progress_render_unit.cpp src/BuildProgress.cpp -o $@

test-progress-render: $(PROGRESS_RENDER_TEST)
	"$(PROGRESS_RENDER_TEST)"

# Offline contract tests for the Snap publication coordinator: architecture set,
# candidate staging, complete-set verification, promotion, rollback and
# fail-closed behaviour. No network access and no Store operations.
test-snap-contract:
	python3 tests/snap_release_contract.py

# POSIX PTY end-to-end progress coverage uses `script`; skipped (exit 77) when
# unavailable, matching the pagination-ordering skip convention. Not part of the
# Windows matrix, where the portable test-progress-render unit test covers the
# same renderer lifecycle.
ifneq ($(OS),Windows_NT)
test-progress-pty: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/progress_pty_smoke.sh; \
	  status=$$?; \
	  if [ $$status -eq 77 ]; then echo "test-progress-pty: skipped (PTY tooling unavailable)"; \
	  else exit $$status; fi
PROGRESS_PTY_TARGET := test-progress-pty
endif

test-diagnostics: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/diagnostics_smoke.sh

test-minify:
	$(MAKE) -C minifypp test-smoke

test-json-schema-integration: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/json_schema_integration_smoke.sh

test-content: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/parser_content_smoke.sh

ENGINE_TEST := $(TEST_DIR)/engine-smoke$(EXEEXT)
ENGINE_CORE_OBJECTS := $(filter-out src/nift.o src/CLI.o,$(OBJECTS))

$(ENGINE_TEST): tests/engine_smoke.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_smoke.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine: $(ENGINE_TEST)
	$(ENGINE_TEST)

OWNERSHIP_UNIT_TEST := $(TEST_DIR)/ownership-unit$(EXEEXT)
$(OWNERSHIP_UNIT_TEST): tests/ownership_unit.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/ownership_unit.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-ownership-unit: $(OWNERSHIP_UNIT_TEST)
	$(OWNERSHIP_UNIT_TEST)

ENGINE_BINDINGS_TEST := $(TEST_DIR)/engine-bindings$(EXEEXT)
$(ENGINE_BINDINGS_TEST): tests/engine_bindings.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_bindings.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-bindings: $(ENGINE_BINDINGS_TEST)
	$(ENGINE_BINDINGS_TEST)

ENGINE_RENDER_API_TEST := $(TEST_DIR)/engine-render-api$(EXEEXT)
$(ENGINE_RENDER_API_TEST): tests/engine_render_api.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_render_api.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-render-api: $(ENGINE_RENDER_API_TEST)
	$(ENGINE_RENDER_API_TEST)

# Public-header consumer probe: compiled with ONLY the public include path, so
# it proves <nift/nift.h> is self-contained (no -Isrc, no Jsonic++ visibility).
PUBLIC_HEADER_PROBE := $(TEST_DIR)/public-header-probe$(EXEEXT)
$(PUBLIC_HEADER_PROBE): tests/public_header_probe.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) -std=c++17 -Iinclude tests/public_header_probe.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-public-header: $(PUBLIC_HEADER_PROBE)
	$(PUBLIC_HEADER_PROBE)

ENGINE_LOADERS_TEST := $(TEST_DIR)/engine-loaders$(EXEEXT)
$(ENGINE_LOADERS_TEST): tests/engine_loaders.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_loaders.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

ENGINE_SOURCE_READ_TEST := $(TEST_DIR)/engine-source-read$(EXEEXT)
$(ENGINE_SOURCE_READ_TEST): tests/engine_source_read.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_source_read.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-source-read: $(ENGINE_SOURCE_READ_TEST)
	$(ENGINE_SOURCE_READ_TEST)

test-engine-loaders: $(ENGINE_LOADERS_TEST)
	$(ENGINE_LOADERS_TEST)

ENGINE_PATHTO_TEST := $(TEST_DIR)/engine-pathto$(EXEEXT)
$(ENGINE_PATHTO_TEST): tests/engine_pathto.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_pathto.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-pathto: $(ENGINE_PATHTO_TEST)
	$(ENGINE_PATHTO_TEST)

# PA1: read-only ProjectState must match ProjectInfo's read semantics exactly,
# never write to disk, and keep shared read caches safe under concurrency.
PROJECT_STATE_TEST := $(TEST_DIR)/project-state-parity$(EXEEXT)
$(PROJECT_STATE_TEST): tests/project_state_parity.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/project_state_parity.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-project-state: $(PROJECT_STATE_TEST)
	$(PROJECT_STATE_TEST)

# PA2: ProjectHost adapts the ProjectState snapshot to RenderHost so the
# existing Parser renders real project pages (content/template/input, JSON,
# contracts, tracked output lookup, @pathto geometry incl. 404, pagination)
# with zero writes and no build decisions.
PROJECT_HOST_TEST := $(TEST_DIR)/project-host$(EXEEXT)
$(PROJECT_HOST_TEST): tests/project_host.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/project_host.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-project-host: $(PROJECT_HOST_TEST)
	$(PROJECT_HOST_TEST)

# PA3: the public project-aware Engine API - explicit Engine(root) construction,
# render("page-name"[, context]), controlled failure behaviour, defaults/Context
# overlay/environment precedence, dependency/requirement reporting, zero writes.
ENGINE_PROJECT_TEST := $(TEST_DIR)/engine-project$(EXEEXT)
$(ENGINE_PROJECT_TEST): tests/engine_project.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_project.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-project: $(ENGINE_PROJECT_TEST)
	$(ENGINE_PROJECT_TEST)

# PA5: portable project-semantics conformance corpus. cpp_runner renders pages
# through the public project-aware Engine; run_conformance.py runs every case
# under tests/conformance/cases/ against both the Nift CLI and the Engine and
# checks observable parity (byte-identical output + dependency/requirement sets)
# and accept/reject parity.
CPP_RUNNER := $(TEST_DIR)/cpp-runner$(EXEEXT)
$(CPP_RUNNER): tests/conformance/cpp_runner.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/conformance/cpp_runner.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-conformance: $(CPP_RUNNER) $(TARGET)
	CPP_RUNNER="$(CURDIR)/$(TEST_DIR)/cpp-runner" NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/conformance/run_conformance.py

# PA4: atomic immutable snapshot replacement - reload() keeps in-flight renders
# on their snapshot, retains the last good snapshot on failure, zero writes,
# concurrent render+reload safety, defaults/environment survival.
ENGINE_RELOAD_TEST := $(TEST_DIR)/engine-reload$(EXEEXT)
$(ENGINE_RELOAD_TEST): tests/engine_reload.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_reload.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-reload: $(ENGINE_RELOAD_TEST)
	$(ENGINE_RELOAD_TEST)

# CP8.1: pagination-specific snapshot/reload invariant. A paginated render's
# complete page set must come from one immutable snapshot; a deterministic
# environment-provider barrier (an @getenv("BARRIER") in the pagination
# template) interleaves reload() during pagination assembly -- after the
# snapshot is captured and before the complete multi-page RenderResult exists
# -- and asserts the single result is entirely one generation, the next render
# sees the new generation, and a failed reload retains the last known-good
# pagination generation.
ENGINE_PAGINATION_SNAPSHOT_TEST := $(TEST_DIR)/engine-pagination-snapshot$(EXEEXT)
$(ENGINE_PAGINATION_SNAPSHOT_TEST): tests/engine_pagination_snapshot.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_pagination_snapshot.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-pagination-snapshot: $(ENGINE_PAGINATION_SNAPSHOT_TEST)
	$(ENGINE_PAGINATION_SNAPSHOT_TEST)

ENGINE_CONCURRENCY_TEST := $(TEST_DIR)/engine-concurrency$(EXEEXT)
$(ENGINE_CONCURRENCY_TEST): tests/engine_concurrency.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_concurrency.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-concurrency: $(ENGINE_CONCURRENCY_TEST)
	$(ENGINE_CONCURRENCY_TEST)

# ThreadSanitizer variant: the core objects are rebuilt with -fsanitize=thread
# (the Makefile's TSAN_OBJECTS already do this for all sources) and the
# concurrency test links against them (minus the CLI/main objects).
TSAN_CORE_OBJECTS := $(filter-out $(TEST_DIR)/tsan/src/nift.o $(TEST_DIR)/tsan/src/CLI.o,$(TSAN_OBJECTS))
ENGINE_CONCURRENCY_TSAN := $(TEST_DIR)/engine-concurrency-tsan$(EXEEXT)
$(ENGINE_CONCURRENCY_TSAN): tests/engine_concurrency.cpp $(TSAN_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) -std=c++17 -pthread $(TSAN_FLAGS) tests/engine_concurrency.cpp $(TSAN_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-concurrency-tsan: $(ENGINE_CONCURRENCY_TSAN)
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 $(ENGINE_CONCURRENCY_TSAN)

ENGINE_RELOAD_TSAN := $(TEST_DIR)/engine-reload-tsan$(EXEEXT)
$(ENGINE_RELOAD_TSAN): tests/engine_reload.cpp $(TSAN_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) -std=c++17 -pthread $(TSAN_FLAGS) tests/engine_reload.cpp $(TSAN_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-reload-tsan: $(ENGINE_RELOAD_TSAN)
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 $(ENGINE_RELOAD_TSAN)


# CP10: Nift Embed C ABI. Static + shared libraries over the engine core, and
# an adversarial/lifetime test that exercises only the public C header.
C_ABI_CORE := $(filter-out src/nift.o src/CLI.o,$(OBJECTS))
C_ABI_PIC := $(patsubst %.o,$(TEST_DIR)/pic/%.o,$(C_ABI_CORE))

$(TEST_DIR)/pic/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fPIC -c $< -o $@

libnift_c.a: $(C_ABI_CORE)
	ar rcs $@ $(C_ABI_CORE)

libnift_c.so: $(C_ABI_PIC)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(C_ABI_PIC)

# macOS dynamic library: relocatable @rpath install name so a consumer that
# links it can load it from an installed prefix without absolute paths.
libnift_c.dylib: $(C_ABI_PIC)
	$(CXX) $(CXXFLAGS) -shared -Wl,-install_name,@rpath/libnift_c.dylib -o $@ $(C_ABI_PIC)

C_ABI_TEST := $(TEST_DIR)/c-abi-adversarial$(EXEEXT)
$(C_ABI_TEST): tests/c_abi_adversarial.cpp libnift_c.a
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/c_abi_adversarial.cpp libnift_c.a $(LDLIBS) -o $@

test-c-abi: $(C_ABI_TEST)
	$(C_ABI_TEST)

# Pure-C consumer proof: the public header must compile as C and the static
# library must link into a C program.
C_ABI_C_SMOKE := $(TEST_DIR)/c-abi-smoke$(EXEEXT)
$(C_ABI_C_SMOKE): tests/c_abi_smoke.c libnift_c.a
	mkdir -p $(TEST_DIR)
	$(CC) $(CPPFLAGS) tests/c_abi_smoke.c libnift_c.a $(LDLIBS) -lstdc++ -lm -pthread -o $@

test-c-abi-c-smoke: $(C_ABI_C_SMOKE)
	$(C_ABI_C_SMOKE)

# ---------------------------------------------------------------------------
# Embedded Nift: explicit build targets. None of these run during plain `make`
# (which builds only the reduced ordinary CLI).
# ---------------------------------------------------------------------------

# Native embedded-Nift library (headers live in include/nift/). Also stages the
# installed-prefix layout (dist/embed-prefix) so the Go binding can link via
# pkg-config against a self-contained prefix.
embed: libnift_c.a $(SHARED_LIB) embed-prefix

embed-prefix: libnift_c.a $(SHARED_LIB)
	rm -rf dist/embed-prefix
	mkdir -p dist/embed-prefix/include/nift dist/embed-prefix/lib/pkgconfig
	cp include/nift/*.h dist/embed-prefix/include/nift/
	cp libnift_c.a $(SHARED_LIB) dist/embed-prefix/lib/
	# Dev prefix: the version here is not release metadata (the real per-release
	# nift.pc is generated by packaging/stage-release.sh with the validated
	# version). 0.0.0-dev avoids running the CLI binary inside make (fragile on
	# Windows); ABI compatibility is governed by the C ABI version.
	bash packaging/gen-dev-pc.sh

go-binding: embed
	cd bindings/go && PKG_CONFIG_PATH="$(CURDIR)/dist/embed-prefix/lib/pkgconfig" go build -o embed-harness ./cmd/embed-harness

csharp-binding: libnift_c.so
	cd bindings/csharp/apps/NiftEmbedHarness && dotnet build -v q --nologo

node-binding:
	cd bindings/node && bash build.sh

python-binding:
	cd bindings/python && bash build.sh

bindings: go-binding csharp-binding node-binding python-binding

# Durable build-boundary gate: plain `make`/`make nift` must build ONLY the
# reduced CLI (no src/embed/* objects, no libnift_c, no bindings). Fails if a
# future source glob pulls embedding implementation into the CLI. The gate runs
# in a temporary clean source tree and never writes to the caller's checkout.
test-build-boundary:
	bash tests/build_boundary.sh

# External proof that the boundary gate performs no writes in the caller's
# checkout (before/after filesystem-state comparison).
test-build-boundary-nondestructive:
	bash tests/build_boundary_nondestructive.sh

# Focused embed/binding test targets (mirror the build separation).
test-embed: test-c-abi test-c-abi-c-smoke test-engine test-engine-bindings \
	test-engine-render-api test-conformance

test-go-binding: go-binding
	cd bindings/go && PKG_CONFIG_PATH="$(CURDIR)/dist/embed-prefix/lib/pkgconfig" go test -race ./...

test-csharp-binding: csharp-binding
	cd bindings/csharp/tests/Nift.Tests && dotnet run -v q --nologo

test-node-binding: node-binding
	cd bindings/node && node --test test/nift.test.js

test-python-binding: python-binding
	cd bindings/python && python3 -m unittest tests.test_nift

test-bindings: test-go-binding test-csharp-binding test-node-binding test-python-binding

# The build-boundary gate is NON-DESTRUCTIVE (it runs in a temporary clean
# source tree, never in the caller's checkout), so it is safe under parallel
# Make; prerequisite order carries no sequencing meaning.
test-all: test test-embed test-bindings test-build-boundary

# Plain `make test` = the ordinary Nift/CLI regression surface (C++ toolchain
# only). Embedding and binding suites are run through the focused targets.
test: test-content test-commands test-comments test-contracts test-json \
	test-json-schema test-console test-diagnostics test-minify \
	test-json-schema-integration test-pagination test-pagination-ordering \
	test-template-optional test-requirements test-path-safety test-metadata-safety \
	test-init-targets test-control-flow test-cross-feature test-config-validation \
	test-zero-mutation test-repair-campaign test-ownership-concurrency \
	test-progress-render $(PROGRESS_PTY_TARGET) test-snap-contract

# CP10.2: Embed host-seam failure contract (C++ Engine level).
HOST_SEAM_TEST := $(TEST_DIR)/host-seam$(EXEEXT)
$(HOST_SEAM_TEST): tests/host_seam.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/host_seam.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-host-seam: $(HOST_SEAM_TEST)
	$(HOST_SEAM_TEST)

# CP10: direct C++ Engine::render vs C ABI render overhead benchmark.
C_ABI_BENCH := $(TEST_DIR)/c-abi-bench$(EXEEXT)
$(C_ABI_BENCH): tests/c_abi_bench.cpp libnift_c.a
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/c_abi_bench.cpp libnift_c.a $(LDLIBS) -o $@

benchmark-c-abi: $(C_ABI_BENCH)
	$(C_ABI_BENCH)


test-comments: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/comments_smoke.sh

test-json-binding: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/json_binding_smoke.sh

test-control-flow: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/control_flow_smoke.sh

test-collections: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/collection_ops_smoke.sh

test-commands: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/commands_smoke.sh

test-ownership-concurrency: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/ownership_concurrency.py

test-zero-mutation: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/zero_mutation_smoke.py

test-repair-campaign: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/repair_campaign.py

test-pagination-ordering: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/pagination_ordering_smoke.sh

test-pagination: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/pagination_smoke.sh

test-pagination-equivalence: $(TARGET)
	python3 tests/pagination_incremental_equivalence.py --nift "$(CURDIR)/$(TARGET)"

# BH4: deterministic adversarial incremental state-transition sequence across
# modified/hash/hybrid modes; asserts exact output set (no stale, no missing)
# and incremental==clean equivalence.
test-incremental-state-transitions: $(TARGET)
	python3 tests/incremental_state_transitions_adversarial.py --nift "$(CURDIR)/$(TARGET)"

# BH5: parser / value / composition adversarial; each case must resolve to a
# controlled outcome (success with correct output, or a controlled error) -
# never a hang, signal, sanitizer finding, or missing output.
test-parser-value-composition: $(TARGET)
	python3 tests/parser_value_composition_adversarial.py --nift "$(CURDIR)/$(TARGET)"

# BH6: init/starter functional truth; the scaffold must be internally
# consistent, a clean rebuild must reproduce the init'd output, and builds
# must be idempotent.
test-init-functional-truth: $(TARGET)
	python3 tests/init_scaffold_functional_truth.py --nift "$(CURDIR)/$(TARGET)"

# BH7: persistence/crash/recovery adversarial; SIGKILL mid-build must leave
# crash-safe metadata, a succeeding next build, and output that converges.
test-crash-recovery: $(TARGET)
	python3 tests/crash_recovery_adversarial.py --nift "$(CURDIR)/$(TARGET)"

# BH8: performance/complexity invariants; a no-op build rewrites nothing and a
# one-page change rebuilds exactly that page (change-proportional).
test-complexity-invariants: $(TARGET)
	python3 tests/complexity_invariants.py --nift "$(CURDIR)/$(TARGET)"

# BH9: platform/filesystem boundary; build output is contained within the
# output directory and a read-only output dir fails controlled.
test-filesystem-boundary: $(TARGET)
	python3 tests/filesystem_boundary_adversarial.py --nift "$(CURDIR)/$(TARGET)"

# Config validation guard: unknown .nift/config.json keys (legacy or typo)
# must be rejected loudly rather than silently ignored.
test-config-validation: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/config_validation.sh

# @pathto on the tracked page `404` must emit root-absolute web paths because a
# 404 document is served at arbitrary request depth; checking is unchanged.
test-pathto-404: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/pathto_404_smoke.sh

# Tracked outputs deterministically preserve the source content file's
# permissions (executable scripts stay executable); rebuilds do not depend on
# the output's prior mode.
test-output-permissions: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/output_permissions_smoke.sh

# `nift init --handover` writes a project-root HANDOVER.md byte-for-byte
# identical to the canonical copy (tests/fixtures/HANDOVER.md). Plain init
# must not create it, and it must never land under the output directory.
test-init-handover: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/init_handover_smoke.sh

# Network-gated: the vendored canonical handover must match the live download
# from https://nift.dev/HANDOVER.md. Skipped unless NIFT_LIVE_TESTS=1.
test-handover-live:
	tests/handover_live_check.sh

test-installer:
	tests/install_script_smoke.sh

test-requirements: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/requirements_smoke.sh

test-path-security: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/path_security_smoke.sh

test-path-safety: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/path_safety_smoke.sh

test-metadata-safety: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/metadata_safety_smoke.sh

test-template-optional: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/template_optional_smoke.sh

test-contracts: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/contracts_smoke.sh

test-init-targets: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/init_targets_smoke.sh


test-guarantee-registry:
	python3 scripts/check_guarantee_registry.py

# Single-repository CI variant: asserts everything a Nift-only checkout can
# prove and PASSes; sibling-dependent public-claim surface audit is deferred,
# never silently skipped into green.
test-guarantee-registry-ci:
	python3 scripts/check_guarantee_registry.py --local

test-test-integrity:
	python3 scripts/test_integrity_check.py tests scripts --output "$(TEST_DIR)/bh2/test-integrity-report.json"

BH1_WEBSITE_ROOT ?= ../nift-dev.github.io
BH1_REGRESSION_ROOT ?= ../nift-regression-suite

bh1-guarantee-registry:
	python3 scripts/check_guarantee_registry.py --website-root "$(BH1_WEBSITE_ROOT)" --regression-root "$(BH1_REGRESSION_ROOT)"
	python3 scripts/bh1_registry_liveness.py --website-root "$(BH1_WEBSITE_ROOT)" --regression-root "$(BH1_REGRESSION_ROOT)"

bh2-test-integrity: test-test-integrity test-guarantee-registry-ci test-contracts test-pagination-equivalence test-init-targets test-incremental-state-transitions test-parser-value-composition test-init-functional-truth test-crash-recovery test-complexity-invariants test-filesystem-boundary test-config-validation test-pathto-404 test-output-permissions test-init-handover

# BH3 curated guard mutation / test-of-test tranche 1: applies mutation
# families to exact guard copies, runs them against a real Nift binary (and
# stub/sabotaged substitutes), runs the BH2 static scanner over each mutant,
# and retains the classification report under docs/evidence/bh3/.
bh3-mutation-tranche1:
	mkdir -p docs/evidence/bh3
	python3 scripts/bh3_guard_mutation.py --nift "$(CURDIR)/$(TARGET)" \
		--output docs/evidence/bh3/bh3-mutation-tranche1.json

.PHONY: test-guarantee-registry test-guarantee-registry-ci test-test-integrity bh1-guarantee-registry bh2-test-integrity bh3-mutation-tranche1

install: $(TARGET)
	mkdir -p "$(DESTDIR)$(BINDIR)"
	$(INSTALL_PROGRAM) "$(TARGET)" "$(DESTDIR)$(BINDIR)/$(TARGET)"
	@echo "Installed $(TARGET) to $(DESTDIR)$(BINDIR)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	@echo "Removed $(DESTDIR)$(BINDIR)/$(TARGET)"

clean:
	rm -f $(OBJECTS) $(DEPFILES) "$(TARGET)"
	rm -rf "$(TEST_DIR)"
	rm -f libnift_c.a libnift_c.so bindings/go/embed-harness
	rm -rf bindings/node/build bindings/python/build
	rm -f bindings/python/nift/_nift*.so
	$(MAKE) -C minifypp clean
	$(MAKE) -C jsonic clean

.PHONY: embed go-binding csharp-binding node-binding python-binding bindings test-build-boundary test-embed test-go-binding test-csharp-binding test-node-binding test-python-binding test-bindings test-all test benchmark-memory-10k benchmark-10k test-tracking-scaling test-full-build-scaling test-recovery-epoch test-performance-scaling test-sanitize memory-safety-smoke all clean test-jsonic test-jsonic-sync test-json test-json-schema test-console test-progress-render test-progress-pty test-snap-contract test-diagnostics test-minify test-json-schema-integration test-engine test-engine-bindings test-engine-loaders test-engine-source-read test-engine-pathto test-engine-concurrency test-engine-project test-engine-reload test-engine-pagination-snapshot test-c-abi test-c-abi-c-smoke test-host-seam benchmark-c-abi test-project-state test-project-host test-public-header test-conformance test-content test-commands test-comments test-ownership-concurrency test-zero-mutation test-repair-campaign test-pagination-ordering test-json-binding test-control-flow test-requirements test-path-safety test-metadata-safety test-template-optional test-contracts test-init-targets install uninstall


test-cross-feature: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/cross_feature_smoke.sh


test-incremental-new-features: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/incremental_new_features_smoke.sh


test-state-concurrency: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/persistence_concurrency_failure_smoke.sh


test-minify-integration: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/minify_integration_smoke.sh


test-minify-node:
	$(MAKE) -C minifypp test-node

test-minify-generated:
	$(MAKE) -C minifypp test-generated

test-minify-jsx-generated:
	$(MAKE) -C minifypp test-jsx


test-minify-formats:
	$(MAKE) -C minifypp test-formats

test-minify-cli:
	$(MAKE) -C minifypp test-cli


test-tracking-scaling: $(TARGET)
	python3 tests/tracking_scaling_benchmark.py --nift "$(CURDIR)/$(TARGET)"


test-full-build-scaling: $(TARGET)
	python3 tests/full_build_scaling_failmodes.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/full_build_scaling_benchmark.py --nift "$(CURDIR)/$(TARGET)"


$(RECOVERY_EPOCH_GUARD): tests/recovery_epoch_guard.cpp src/FileSystem.cpp src/FileSystem.h
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_RECOVERY_STATS tests/recovery_epoch_guard.cpp src/FileSystem.cpp -o "$@"


test-recovery-epoch: $(RECOVERY_EPOCH_GUARD)
	"$(RECOVERY_EPOCH_GUARD)" "$(CURDIR)/$(TEST_DIR)/recovery-epoch-fixture"


test-performance-scaling: test-tracking-scaling test-full-build-scaling test-recovery-epoch


benchmark-10k: $(TARGET)
	python3 benchmarks/performance_10k.py --nift "$(CURDIR)/$(TARGET)"


benchmark-memory-10k: $(TARGET)
	python3 tests/memory_10k_benchmark.py --nift "$(CURDIR)/$(TARGET)"


$(TEST_DIR)/san/%.o: %.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) -MMD -MP -c "$<" -o "$@"

$(SAN_TARGET): $(SAN_OBJECTS)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -std=c++17 -pthread $(SANITIZER_FLAGS) $(SAN_OBJECTS) -o "$@"

test-sanitize: $(SAN_TARGET)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(SAN_TARGET)" --version

test-pagination-sanitize: $(SAN_TARGET)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(SAN_TARGET)" tests/pagination_sanitizer_smoke.sh

$(TEST_DIR)/tsan/%.o: %.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(TSAN_FLAGS) -MMD -MP -c "$<" -o "$@"

$(TSAN_TARGET): $(TSAN_OBJECTS)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -std=c++17 -pthread $(TSAN_FLAGS) $(TSAN_OBJECTS) -o "$@"

test-pagination-tsan: $(TSAN_TARGET)
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(TSAN_TARGET)" tests/pagination_sanitizer_smoke.sh

$(MEMORY_SMOKE): tests/json_smoke.cpp src/Json.h
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic $(SANITIZER_FLAGS) tests/json_smoke.cpp -o "$@"

memory-safety-smoke: $(MEMORY_SMOKE)
	mkdir -p "$(TEST_DIR)/memory-safety"
	env -u LD_PRELOAD python3 scripts/memory_safety.py --project nift --mode sanitizer --output "$(TEST_DIR)/memory-safety/checkpoint-0.json" --iterations 2 --command './$(MEMORY_SMOKE)'

# Maintained memory/resource-safety campaign gates.
memory-safety-checkpoint-3: $(SAN_TARGET)
	mkdir -p "$(TEST_DIR)/memory-safety"
	env -u LD_PRELOAD python3 scripts/checkpoint3_core_memory.py --nift "$(CURDIR)/$(SAN_TARGET)" --rounds 4 --output "$(TEST_DIR)/memory-safety/checkpoint-3-core.json"

memory-safety-checkpoint-4-watch: $(TARGET)
	mkdir -p "$(TEST_DIR)/memory-safety"
	python3 scripts/checkpoint4_watch_endurance.py --nift "$(CURDIR)/$(TARGET)" --cycles 180 --interval 0.22 --output "$(TEST_DIR)/memory-safety/checkpoint-4-watch-rss.json"

memory-safety-checkpoint-4-watch-sanitize: $(SAN_TARGET)
	mkdir -p "$(TEST_DIR)/memory-safety"
	env -u LD_PRELOAD python3 scripts/checkpoint4_watch_endurance.py --nift "$(CURDIR)/$(SAN_TARGET)" --cycles 100 --interval 0.22 --output "$(TEST_DIR)/memory-safety/checkpoint-4-watch-sanitizer.json"

memory-safety-checkpoint-4-large: $(TARGET)
	mkdir -p "$(TEST_DIR)/memory-safety"
	python3 scripts/checkpoint4_large_project.py --nift "$(CURDIR)/$(TARGET)" --pages 10000 --output "$(TEST_DIR)/memory-safety/checkpoint-4-large-project.json"

valgrind-memory-safety-checkpoint-4: $(TARGET)
	mkdir -p "$(TEST_DIR)/memory-safety"
	python3 scripts/checkpoint4_watch_endurance.py --nift "$(CURDIR)/scripts/valgrind_nift.sh" --cycles 30 --interval 0.22 --output "$(TEST_DIR)/memory-safety/checkpoint-4-watch-valgrind.json"

.PHONY: memory-safety-checkpoint-3 memory-safety-checkpoint-4-watch memory-safety-checkpoint-4-watch-sanitize memory-safety-checkpoint-4-large valgrind-memory-safety-checkpoint-4

memory-safety-checkpoint-6-sync:
	bash "$(CURDIR)/../jsonic/jsonic/scripts/check-nift-sync.sh" "$(CURDIR)"
	bash "$(CURDIR)/../minify/minify/scripts/check-nift-sync.sh" "$(CURDIR)/minifypp"

memory-safety-checkpoint-6: memory-safety-checkpoint-6-sync $(TARGET)
	mkdir -p .build/memory-safety
	python3 scripts/checkpoint6_integration.py --nift "$(CURDIR)/$(TARGET)" --rounds 60 --pages 90 --output .build/memory-safety/checkpoint-6-integration.json

memory-safety-checkpoint-6-sanitize: memory-safety-checkpoint-6-sync $(SAN_TARGET)
	mkdir -p .build/memory-safety
	env -u LD_PRELOAD python3 scripts/checkpoint6_integration.py --nift "$(CURDIR)/$(SAN_TARGET)" --rounds 12 --pages 30 --output .build/memory-safety/checkpoint-6-integration-sanitizer.json

valgrind-memory-safety-checkpoint-6: memory-safety-checkpoint-6-sync $(TARGET)
	mkdir -p .build/memory-safety
	python3 scripts/checkpoint6_integration.py --valgrind --nift "$(CURDIR)/$(TARGET)" --rounds 12 --pages 40 --output .build/memory-safety/checkpoint-6-valgrind.json

.PHONY: memory-safety-checkpoint-6-sync memory-safety-checkpoint-6 memory-safety-checkpoint-6-sanitize valgrind-memory-safety-checkpoint-6

checkpoint-7-incremental-equivalence: $(TARGET)
	mkdir -p .build/checkpoint-7
	python3 scripts/checkpoint7_incremental_equivalence.py --nift "$(CURDIR)/$(TARGET)" --seeds 8 --steps 30 --output .build/checkpoint-7/incremental-equivalence.json

.PHONY: checkpoint-7-incremental-equivalence

checkpoint-8-filesystem-transaction: $(TARGET)
	mkdir -p .build/checkpoint-8
	python3 scripts/checkpoint8_filesystem_transaction.py --nift "$(CURDIR)/$(TARGET)" --output .build/checkpoint-8/filesystem-transaction.json

.PHONY: checkpoint-8-filesystem-transaction

checkpoint-9-parser-fuzz: $(SAN_TARGET)
	mkdir -p .build/checkpoint-9
	env -u LD_PRELOAD python3 scripts/checkpoint9_parser_fuzz.py --nift "$(CURDIR)/$(SAN_TARGET)" --cases 400 --seeds 9001,17713,424242 --output .build/checkpoint-9/parser-fuzz.json

.PHONY: checkpoint-9-parser-fuzz

checkpoint-10-cross-platform: $(TARGET)
	mkdir -p .build/checkpoint-10
	python3 scripts/checkpoint10_cross_platform.py --nift "$(CURDIR)/$(TARGET)" --output .build/checkpoint-10/$(if $(filter Windows_NT,$(OS)),windows,local).json --runner-os $(if $(filter Windows_NT,$(OS)),Windows,Local)

.PHONY: checkpoint-10-cross-platform
