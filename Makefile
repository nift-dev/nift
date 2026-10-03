CXX ?= g++
CC ?= cc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -pthread
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark
LDFLAGS ?=
LDLIBS ?=

# Shared core + CLI implementation (the ordinary Nift CLI needs only these).
PARSER_SOURCES := $(sort $(wildcard src/Parser*.cpp))
CORE_SOURCES := src/nift.cpp src/ProjectOwnership.cpp src/PackageTransaction.cpp src/CLI.cpp src/Process.cpp src/JobControl.cpp src/RuntimeValue.cpp src/Value.cpp src/FileSystem.cpp src/JsonFile.cpp src/JsonSchema.cpp minifypp/src/Minify.cpp markuppp/src/Markup.cpp markuppp/src/AsciiDoc.cpp markuppp/src/ReStructuredText.cpp $(PARSER_SOURCES) src/ProjectInfo.cpp src/ProjectRead.cpp src/ProjectState.cpp src/WatchList.cpp src/BuildProgress.cpp src/Automation.cpp src/Hooks.cpp src/Ast.cpp
# Embedding-exclusive implementation (Engine, Context, C ABI). The reduced CLI
# never compiles or links these; they are built by the embed library and the
# engine/C ABI test targets.
EMBED_SOURCES := src/embed/Engine.cpp src/embed/Context.cpp src/embed/c_abi.cpp
SOURCES := $(CORE_SOURCES) $(EMBED_SOURCES)
MARKUP_C_NAMES := blocks buffer cmark cmark_ctype houdini_href_e houdini_html_e houdini_html_u html inlines iterator node references render scanners utf8
MARKUP_C_SOURCES := $(addprefix markuppp/vendor/cmark/,$(addsuffix .c,$(MARKUP_C_NAMES)))
MARKUP_C_OBJECTS := $(MARKUP_C_SOURCES:.c=.o)
OBJECTS := $(SOURCES:.cpp=.o) $(MARKUP_C_OBJECTS)
CLI_OBJECTS := $(CORE_SOURCES:.cpp=.o) $(MARKUP_C_OBJECTS)
PARSER_OBJECTS := $(PARSER_SOURCES:.cpp=.o)
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
	LIBFFI_SHARED_LINK_FLAGS := -Wl,--exclude-libs,ALL
else
	EXEEXT :=
	PREFIX ?= /usr/local
	INSTALL_PROGRAM = install -m 0755
	ifeq ($(shell uname -s),Darwin)
		# macOS shared library is a Mach-O .dylib with a relocatable install name.
		SHARED_LIB := libnift_c.dylib
		LIBFFI_SHARED_LINK_FLAGS :=
	else
		SHARED_LIB := libnift_c.so
		LDLIBS += -ldl
		LIBFFI_SHARED_LINK_FLAGS := -Wl,--exclude-libs,ALL -Wl,-Bsymbolic
	endif
endif

TARGET := nift$(EXEEXT)
BINDIR ?= $(PREFIX)/bin
DESTDIR ?=

TEST_DIR := .build
LIBFFI_TARGET := $(shell $(CC) -dumpmachine 2>/dev/null || uname -m)-$(notdir $(CC))
LIBFFI_BUILD := $(TEST_DIR)/libffi/$(LIBFFI_TARGET)
LIBFFI_STAMP := $(LIBFFI_BUILD)/.nift-built
LIBFFI_A := $(LIBFFI_BUILD)/install/lib/libffi.a
LIBFFI_INCLUDE := $(LIBFFI_BUILD)/install/include
LIBFFI_CFLAGS ?= -O2 -fPIC
CPPFLAGS += -I$(LIBFFI_INCLUDE)
SANITIZER_FLAGS ?= -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
SAN_TARGET := $(TEST_DIR)/nift-sanitize$(EXEEXT)
SAN_OBJECTS := $(patsubst %.cpp,$(TEST_DIR)/san/%.o,$(SOURCES)) $(patsubst %.c,$(TEST_DIR)/san/%.o,$(MARKUP_C_SOURCES))
SAN_LIBFFI_BUILD := $(TEST_DIR)/libffi/sanitize-$(LIBFFI_TARGET)
SAN_LIBFFI_STAMP := $(SAN_LIBFFI_BUILD)/.nift-built
SAN_LIBFFI_A := $(SAN_LIBFFI_BUILD)/install/lib/libffi.a
SAN_LIBFFI_INCLUDE := $(SAN_LIBFFI_BUILD)/install/include
SAN_CPPFLAGS = $(filter-out -I$(LIBFFI_INCLUDE),$(CPPFLAGS)) -I$(SAN_LIBFFI_INCLUDE)
TSAN_FLAGS ?= -O1 -g -fno-omit-frame-pointer -fsanitize=thread
TSAN_TARGET := $(TEST_DIR)/nift-tsan$(EXEEXT)
TSAN_OBJECTS := $(patsubst %.cpp,$(TEST_DIR)/tsan/%.o,$(SOURCES)) $(patsubst %.c,$(TEST_DIR)/tsan/%.o,$(MARKUP_C_SOURCES))
TSAN_LIBFFI_BUILD := $(TEST_DIR)/libffi/tsan-$(LIBFFI_TARGET)
TSAN_LIBFFI_STAMP := $(TSAN_LIBFFI_BUILD)/.nift-built
TSAN_LIBFFI_A := $(TSAN_LIBFFI_BUILD)/install/lib/libffi.a
TSAN_LIBFFI_INCLUDE := $(TSAN_LIBFFI_BUILD)/install/include
TSAN_CPPFLAGS = $(filter-out -I$(LIBFFI_INCLUDE),$(CPPFLAGS)) -I$(TSAN_LIBFFI_INCLUDE)
MEMORY_SMOKE := $(TEST_DIR)/nift-memory-san$(EXEEXT)
JSON_TEST := $(TEST_DIR)/nift-json-smoke$(EXEEXT)
JSON_SCHEMA_TEST := $(TEST_DIR)/nift-json-schema-smoke$(EXEEXT)
RUNTIME_VALUE_TEST := $(TEST_DIR)/nift-runtime-value$(EXEEXT)
CP17_BYTES_TEST := $(TEST_DIR)/nift-cp17-bytes$(EXEEXT)
CP18_BYTES_TEST := $(TEST_DIR)/nift-cp18-bytes$(EXEEXT)
CP21_BYTES_EMBED_TEST := $(TEST_DIR)/nift-cp21-bytes-embed$(EXEEXT)
RECOVERY_EPOCH_GUARD := $(TEST_DIR)/nift-recovery-epoch-guard$(EXEEXT)

all: $(TARGET)

FORCE:

# The shipped CLI is the REDUCED object set (no src/embed/*). Plain `make`
# therefore builds only the ordinary Nift CLI; the embedding library and every
# language binding are explicit, optional targets.
$(TARGET): $(CLI_OBJECTS) $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(CLI_OBJECTS) $(LDLIBS) -o $@

$(LIBFFI_STAMP): scripts/build_vendored_libffi.sh scripts/check_vendored_libffi.py third_party/libffi/NIFT-PROVENANCE.md
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(LIBFFI_CFLAGS)" bash scripts/build_vendored_libffi.sh "$(LIBFFI_BUILD)"

libffi-check: $(LIBFFI_STAMP) FORCE
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(LIBFFI_CFLAGS)" bash scripts/build_vendored_libffi.sh "$(LIBFFI_BUILD)"

$(LIBFFI_A): | libffi-check

LDLIBS += $(LIBFFI_A)

$(PARSER_OBJECTS) $(patsubst %.cpp,$(TEST_DIR)/pic/%.o,$(PARSER_SOURCES)): | libffi-check

$(SAN_LIBFFI_STAMP): scripts/build_vendored_libffi.sh scripts/check_vendored_libffi.py third_party/libffi/NIFT-PROVENANCE.md
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(LIBFFI_CFLAGS) $(SANITIZER_FLAGS)" bash scripts/build_vendored_libffi.sh "$(SAN_LIBFFI_BUILD)"

san-libffi-check: $(SAN_LIBFFI_STAMP) FORCE
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(LIBFFI_CFLAGS) $(SANITIZER_FLAGS)" bash scripts/build_vendored_libffi.sh "$(SAN_LIBFFI_BUILD)"

$(SAN_LIBFFI_A): | san-libffi-check
$(patsubst %.cpp,$(TEST_DIR)/san/%.o,$(PARSER_SOURCES)): | san-libffi-check

$(TSAN_LIBFFI_STAMP): scripts/build_vendored_libffi.sh scripts/check_vendored_libffi.py third_party/libffi/NIFT-PROVENANCE.md
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(LIBFFI_CFLAGS) $(TSAN_FLAGS)" bash scripts/build_vendored_libffi.sh "$(TSAN_LIBFFI_BUILD)"

tsan-libffi-check: $(TSAN_LIBFFI_STAMP) FORCE
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(LIBFFI_CFLAGS) $(TSAN_FLAGS)" bash scripts/build_vendored_libffi.sh "$(TSAN_LIBFFI_BUILD)"

$(TSAN_LIBFFI_A): | tsan-libffi-check
$(patsubst %.cpp,$(TEST_DIR)/tsan/%.o,$(PARSER_SOURCES)): | tsan-libffi-check

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

%.o: %.c
	$(CC) -Imarkuppp/vendor/cmark $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPFILES) $(patsubst %.o,%.d,$(SAN_OBJECTS) $(TSAN_OBJECTS))

test-jsonic:
	$(MAKE) -C jsonic test

test-jsonic-sync:
	@test -n "$(JSONIC_DIR)" || (echo "JSONIC_DIR=/path/to/jsonic is required" >&2; exit 2)
	$(MAKE) -C "$(JSONIC_DIR)" check-nift-sync NIFT_DIR="$(CURDIR)"

test-markuppp-sync:
	@test -n "$(MARKUP_DIR)" || (echo "MARKUP_DIR=/path/to/markup is required" >&2; exit 2)
	$(MAKE) -C "$(MARKUP_DIR)" check-nift-sync NIFT_DIR="$(CURDIR)"

test-json:
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/json_smoke.cpp -o "$(JSON_TEST)"
	"$(JSON_TEST)"

test-json-schema:
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/json_schema_smoke.cpp src/JsonSchema.cpp -o "$(JSON_SCHEMA_TEST)"
	"$(JSON_SCHEMA_TEST)"

$(RUNTIME_VALUE_TEST): tests/runtime_value.cpp src/RuntimeValue.cpp src/RuntimeValue.h src/RuntimeJson.h
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/runtime_value.cpp src/RuntimeValue.cpp -o "$@"

test-runtime-value: $(RUNTIME_VALUE_TEST)
	"$(RUNTIME_VALUE_TEST)"

$(CP17_BYTES_TEST): tests/cp17_bytes.cpp src/RuntimeValue.cpp src/RuntimeValue.h
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/cp17_bytes.cpp src/RuntimeValue.cpp -o "$@"

test-cp17-bytes: $(TARGET) $(CP17_BYTES_TEST)
	"$(CP17_BYTES_TEST)"
	NIFT="$(CURDIR)/$(TARGET)" tests/cp17_bytes.sh

test-cp19-bytes: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/cp19_bytes_io.sh

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

# Fail-closed version-consistency gate: the executable version in src/CLI.cpp
# and the Snap metadata version in snap/snapcraft.yaml must agree, and any
# expected/tag version must match both. No network access.
test-version-consistency:
	python3 tests/version_consistency_test.py
	python3 scripts/check_version_consistency.py

# Focused unit tests for the Distribution Verification summary classification:
# Snap edge must never be reported as stable success, and stable/edge/mismatch/
# install-runtime states are distinguished. No network access.
test-distribution-summary:
	python3 tests/distribution_summary_test.py

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
PARSER_STATEMENT_STATE_TEST := $(TEST_DIR)/parser-statement-state-unit$(EXEEXT)
DIAGNOSTIC_OUTCOME_TEST := $(TEST_DIR)/diagnostic-outcome-unit$(EXEEXT)
CP3_EMBED_TEST := $(TEST_DIR)/v46-b4-cp3-embed$(EXEEXT)
CP8_EMBED_TEST := $(TEST_DIR)/v46-b4-cp8-embed$(EXEEXT)
PACKAGE_GRAPH_LOCK_TEST := $(TEST_DIR)/package-graph-lock-unit$(EXEEXT)
PACKAGE_GRAPH_RESOLVER_TEST := $(TEST_DIR)/package-graph-resolver-unit$(EXEEXT)

$(DIAGNOSTIC_OUTCOME_TEST): tests/diagnostic_outcome_unit.cpp src/Diagnostic.h src/Outcome.h src/RuntimeValue.cpp src/RuntimeValue.h
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/diagnostic_outcome_unit.cpp src/RuntimeValue.cpp -o $@

$(CP3_EMBED_TEST): tests/v46_b4_cp3_embed.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v46_b4_cp3_embed.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(CP8_EMBED_TEST): tests/v46_b4_cp8_embed.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v46_b4_cp8_embed.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(PACKAGE_GRAPH_LOCK_TEST): tests/package_graph_lock_unit.cpp src/PackageGraphLock.h src/PackageMetadata.h src/FileSystem.cpp src/JsonFile.cpp
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/package_graph_lock_unit.cpp src/FileSystem.cpp src/JsonFile.cpp -o $@

$(PACKAGE_GRAPH_RESOLVER_TEST): tests/package_graph_resolver_unit.cpp src/PackageGraphResolver.h src/PackageGraphLock.h src/PackageMetadata.h src/FileSystem.cpp src/JsonFile.cpp
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/package_graph_resolver_unit.cpp src/FileSystem.cpp src/JsonFile.cpp -o $@

$(PARSER_STATEMENT_STATE_TEST): tests/parser_statement_state_unit.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/parser_statement_state_unit.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

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

$(CP18_BYTES_TEST): tests/cp18_bytes.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/cp18_bytes.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-cp18-bytes: $(TARGET) $(CP18_BYTES_TEST)
	$(CP18_BYTES_TEST)
	NIFT="$(CURDIR)/$(TARGET)" tests/cp18_bytes.sh

ENGINE_RENDER_API_TEST := $(TEST_DIR)/engine-render-api$(EXEEXT)
$(ENGINE_RENDER_API_TEST): tests/engine_render_api.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_render_api.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine-render-api: $(ENGINE_RENDER_API_TEST)
	$(ENGINE_RENDER_API_TEST)

# Public-header consumer probe: compiled with ONLY the public include path, so
# it proves <nift/nift.h> is self-contained (no -Isrc, no Jsonic++ visibility).
PUBLIC_HEADER_PROBE := $(TEST_DIR)/public-header-probe$(EXEEXT)
$(PUBLIC_HEADER_PROBE): tests/public_header_probe.cpp $(wildcard include/nift/*.h) $(ENGINE_CORE_OBJECTS)
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
# contracts, tracked output lookup, @path geometry incl. 404, pagination)
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
$(ENGINE_CONCURRENCY_TSAN): tests/engine_concurrency.cpp $(TSAN_CORE_OBJECTS) $(TSAN_LIBFFI_A)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) -std=c++17 -pthread $(TSAN_FLAGS) tests/engine_concurrency.cpp $(TSAN_CORE_OBJECTS) $(TSAN_LIBFFI_A) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@

test-engine-concurrency-tsan: $(ENGINE_CONCURRENCY_TSAN)
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 $(ENGINE_CONCURRENCY_TSAN)

ENGINE_RELOAD_TSAN := $(TEST_DIR)/engine-reload-tsan$(EXEEXT)
$(ENGINE_RELOAD_TSAN): tests/engine_reload.cpp $(TSAN_CORE_OBJECTS) $(TSAN_LIBFFI_A)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) -std=c++17 -pthread $(TSAN_FLAGS) tests/engine_reload.cpp $(TSAN_CORE_OBJECTS) $(TSAN_LIBFFI_A) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@

test-engine-reload-tsan: $(ENGINE_RELOAD_TSAN)
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 $(ENGINE_RELOAD_TSAN)


# CP10: Nift Embed C ABI. Static + shared libraries over the engine core, and
# an adversarial/lifetime test that exercises only the public C header.
C_ABI_CORE := $(filter-out src/nift.o src/CLI.o,$(OBJECTS))
C_ABI_PIC := $(patsubst %.o,$(TEST_DIR)/pic/%.o,$(C_ABI_CORE))

$(TEST_DIR)/pic/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fPIC -MMD -MP -c $< -o $@

$(TEST_DIR)/pic/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) -Imarkuppp/vendor/cmark $(CFLAGS) -fPIC -MMD -MP -c $< -o $@

-include $(C_ABI_PIC:.o=.d)

libnift_c.a: $(C_ABI_CORE) $(LIBFFI_A)
	cp $(LIBFFI_A) $@
	ar rcs $@ $(C_ABI_CORE)

libnift_c.so: $(C_ABI_PIC) $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) -shared $(LIBFFI_SHARED_LINK_FLAGS) -o $@ $(C_ABI_PIC) $(LIBFFI_A)

# macOS dynamic library: relocatable @rpath install name so a consumer that
# links it can load it from an installed prefix without absolute paths.
libnift_c.dylib: $(C_ABI_PIC) $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) -shared $(LIBFFI_SHARED_LINK_FLAGS) -Wl,-install_name,@rpath/libnift_c.dylib -o $@ $(C_ABI_PIC) $(LIBFFI_A)

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

$(CP21_BYTES_EMBED_TEST): tests/cp21_bytes_embed.cpp libnift_c.a
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/cp21_bytes_embed.cpp libnift_c.a $(LDLIBS) -o $@

test-cp21-bytes: $(CP21_BYTES_EMBED_TEST)
	$(CP21_BYTES_EMBED_TEST)
	cmp -s include/nift/c_abi.h bindings/go/cabi/include/nift/c_abi.h

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
V46_TIME_EMBED_TEST := $(TEST_DIR)/v46-time-embed$(EXEEXT)
V46_TIMER_UNIT_TEST := $(TEST_DIR)/v46-timer-unit$(EXEEXT)
V46_TIMER_UNIT_SAN_TEST := $(TEST_DIR)/v46-timer-unit-sanitize$(EXEEXT)
V46_TIMER_EMBED_SAN_TEST := $(TEST_DIR)/v46-timer-embed-sanitize$(EXEEXT)
V46_SECURE_RANDOM_EMBED_TEST := $(TEST_DIR)/v46-secure-random-embed$(EXEEXT)
V46_OUTPUT_EMBED_TEST := $(TEST_DIR)/v46-output-embed$(EXEEXT)
V46_RESOURCE_PATHS_EMBED_TEST := $(TEST_DIR)/v46-resource-paths-embed$(EXEEXT)
V46_RESOURCE_PATHS_EMBED_SAN_TEST := $(TEST_DIR)/v46-resource-paths-embed-sanitize$(EXEEXT)
test-embed: test-c-abi test-c-abi-c-smoke test-cp21-bytes test-engine test-engine-bindings test-public-header \
	test-engine-render-api test-conformance test-v45-embed-contracts test-v45-embed-staged-consumer test-v46-time-embed test-v46-secure-random-embed test-v46-output-embed test-v46-resource-paths-embed

V45_EMBED_CONTRACT_TEST := $(TEST_DIR)/v45-embed-contract$(EXEEXT)
V45_EMBED_SCRIPT_TEST := $(TEST_DIR)/v45-embed-script$(EXEEXT)
V45_EMBED_HOST_CALLABLES_TEST := $(TEST_DIR)/v45-embed-host-callables$(EXEEXT)
V45_EMBED_CONCURRENCY_TEST := $(TEST_DIR)/v45-embed-concurrency$(EXEEXT)
V45_EMBED_SCRIPT_C_TEST := $(TEST_DIR)/v45-embed-script-c$(EXEEXT)
V45_EMBED_SCRIPT_C_OBJECT := $(TEST_DIR)/v45-embed-script-c.o
V45_EMBED_PUBLIC_HEADERS := $(wildcard include/nift/*.h)

$(V45_EMBED_CONTRACT_TEST): tests/v45_embed_contract.cpp $(V45_EMBED_PUBLIC_HEADERS) $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v45_embed_contract.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(V45_EMBED_SCRIPT_TEST): tests/v45_embed_script.cpp $(V45_EMBED_PUBLIC_HEADERS) $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v45_embed_script.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(V45_EMBED_HOST_CALLABLES_TEST): tests/v45_embed_host_callables.cpp $(V45_EMBED_PUBLIC_HEADERS) $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v45_embed_host_callables.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(V45_EMBED_CONCURRENCY_TEST): tests/v45_embed_concurrency.cpp $(V45_EMBED_PUBLIC_HEADERS) $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v45_embed_concurrency.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(V45_EMBED_SCRIPT_C_OBJECT): tests/v45_embed_script_c.c $(V45_EMBED_PUBLIC_HEADERS)
	mkdir -p $(TEST_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(V45_EMBED_SCRIPT_C_TEST): $(V45_EMBED_SCRIPT_C_OBJECT) libnift_c.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(V45_EMBED_SCRIPT_C_OBJECT) libnift_c.a $(LDLIBS) -o $@

$(V46_TIME_EMBED_TEST): tests/v46_time_embed.cpp $(V45_EMBED_PUBLIC_HEADERS) $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v46_time_embed.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(V46_TIMER_UNIT_TEST): tests/v46_timer_unit.cpp $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v46_timer_unit.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(V46_TIMER_UNIT_SAN_TEST): tests/v46_timer_unit.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A)
	mkdir -p $(TEST_DIR)
	$(CXX) $(SAN_CPPFLAGS) $(LDFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) tests/v46_timer_unit.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@

$(V46_TIMER_EMBED_SAN_TEST): tests/v46_time_embed.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A)
	mkdir -p $(TEST_DIR)
	$(CXX) $(SAN_CPPFLAGS) $(LDFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) tests/v46_time_embed.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@

$(V46_SECURE_RANDOM_EMBED_TEST): tests/v46_secure_random_embed.cpp $(V45_EMBED_PUBLIC_HEADERS) $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v46_secure_random_embed.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(V46_OUTPUT_EMBED_TEST): tests/v46_output_embed.cpp $(V45_EMBED_PUBLIC_HEADERS) $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v46_output_embed.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(V46_RESOURCE_PATHS_EMBED_TEST): tests/v46_resource_paths_embed.cpp $(V45_EMBED_PUBLIC_HEADERS) $(ENGINE_CORE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/v46_resource_paths_embed.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

$(V46_RESOURCE_PATHS_EMBED_SAN_TEST): tests/v46_resource_paths_embed.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A)
	mkdir -p $(TEST_DIR)
	$(CXX) $(SAN_CPPFLAGS) $(LDFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) tests/v46_resource_paths_embed.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@

test-v46-time-embed: $(V46_TIME_EMBED_TEST)
	$(V46_TIME_EMBED_TEST)

test-v45-embed-contracts: $(V45_EMBED_CONTRACT_TEST) $(V45_EMBED_SCRIPT_TEST) $(V45_EMBED_HOST_CALLABLES_TEST) $(V45_EMBED_CONCURRENCY_TEST) $(V45_EMBED_SCRIPT_C_TEST)
	$(V45_EMBED_CONTRACT_TEST)
	$(V45_EMBED_SCRIPT_TEST)
	$(V45_EMBED_HOST_CALLABLES_TEST)
	$(V45_EMBED_CONCURRENCY_TEST)
	$(V45_EMBED_SCRIPT_C_TEST)

test-v45-embed-staged-consumer: embed
	bash tests/v45_embed_staged_consumer.sh

ifneq ($(OS),Windows_NT)
test-v45-integration-dogfood: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v45_integration_dogfood.sh

test-v45-adversarial-runtime: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/v45_adversarial_runtime.sh
else
test-v45-integration-dogfood:
	@echo "test-v45-integration-dogfood: skipped (POSIX shared-library, shebang, and job-control coverage)"

test-v45-adversarial-runtime:
	@echo "test-v45-adversarial-runtime: skipped (POSIX timeout and job-control coverage)"
endif

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
test: test-content test-commands test-comments test-contracts test-json test-runtime-value test-cp15-numeric-repair test-cp17-bytes test-cp18-bytes test-cp19-bytes test-cp20-bytes test-cp21-bytes \
	test-json-schema test-console test-diagnostics test-minify \
	test-json-schema-integration test-markup-json-directives test-pagination test-pagination-ordering \
	test-template-optional test-requirements test-path-alias test-path-safety test-metadata-safety \
	test-init-targets test-init-lock test-control-flow test-template-variables test-cross-feature test-v41-certification test-v42-language test-v42-struct test-v43-language test-config-validation \
	test-zero-mutation test-repair-campaign test-ownership-concurrency \
	test-macos-runner-policy \
	test-v44-execution-shell test-v44-language-foundation test-v44-shell-restricted \
	test-v46-time-cli test-v46-timer test-v46-secure-random-cli test-v46-output-cli test-v46-relative-imports test-progress-render $(PROGRESS_PTY_TARGET) test-snap-contract test-distribution-summary test-version-consistency test-unreadable-source test-incremental-modified-immediate

test-cp15-numeric-repair: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/cp15_numeric_repair.sh

FFI_ABI_TEST := $(TEST_DIR)/nift-ffi-abi$(EXEEXT)
$(FFI_ABI_TEST): tests/ffi_abi.cpp src/FfiAbi.h $(LIBFFI_A)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/ffi_abi.cpp $(LIBFFI_A) -o $@

test-ffi-abi: $(FFI_ABI_TEST)
	$(FFI_ABI_TEST)

test-libffi-source:
	python3 scripts/check_vendored_libffi.py

test-gate6ar-ffi: $(TARGET) test-ffi-abi
	NIFT="$(CURDIR)/$(TARGET)" bash tests/gate6ar_ffi.sh

test-v45-ffi: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v45_ffi_contract.sh
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v45_ffi_scalar.sh
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v45_ffi_memory_callback.sh
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v45_ffi_package.sh
	NIFT="$(CURDIR)/$(TARGET)" bash tests/gate6ar_ffi.sh

test-libffi-static-archive: libnift_c.a
	bash tests/libffi_static_consumer.sh

test-libffi-dependencies: $(TARGET) embed node-binding python-binding
	bash scripts/audit_no_dynamic_libffi.sh "$(TARGET)" "$(SHARED_LIB)" bindings/node/build/nift_node.node bindings/python/nift/_nift*.so
	bash scripts/audit_private_libffi.sh "$(SHARED_LIB)" bindings/node/build/nift_node.node bindings/python/nift/_nift*.so

test-libffi-private-symbols: embed node-binding python-binding
	bash scripts/audit_private_libffi.sh "$(SHARED_LIB)" bindings/node/build/nift_node.node bindings/python/nift/_nift*.so
	bash tests/libffi_private_audit.sh

test-pic-depfiles: $(patsubst %.cpp,$(TEST_DIR)/pic/%.o,$(PARSER_SOURCES))
	LIBFFI_INCLUDE="$(LIBFFI_INCLUDE)" bash tests/pic_depfiles.sh

test-node-package-licenses:
	python3 scripts/check_node_package_licenses.py

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

test-markup-json-directives: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/markup_json_directives_smoke.sh

test-control-flow: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/control_flow_smoke.sh
test-template-variables: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/template_variables_smoke.sh


test-v41-certification: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v41_certification_adversarial.sh

	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v41_inject_dependency.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v41_language_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v41_operator_smoke.sh

# Nift v4.2 structured-control/function-program language tranche.
test-v42-language: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_null_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_while_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_continue_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_break_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_return_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_fragment_return_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_control_adversarial.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_function_program_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_numeric_literals_smoke.sh

test-v42-struct: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_definition_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_instance_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_fields_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_constructor_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_methods_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_this_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_private_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_copy_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_deepcopy_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_member_expression_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v42_struct_adversarial.sh

# Nift v4.3 language ergonomics campaign (CP0-CP44) focused tests.
test-v43-language: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp0_cp14_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp15_cp34_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp35_cp44_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_review_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp51_cp70_scripting_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/script_import_syntax.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp71_cp87_native_io_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp78_streams_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp85_repl_multiline_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp88_repl_lifetime_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp90_scripting_wall_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp94_cp97_inspection_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp100_cp103_string_expr_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp105_cp113_filevalue_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp112_filevalue_wall_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_pay_for_use_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_readval_writeval_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_collection_ops_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_postfix_composition_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_hierarchy_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_hierarchy_incremental_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_hierarchy_adversarial_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_hierarchy_pay_for_use_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_mundane_surface_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_frontend_surface_dogfood.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_surface_robustness_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_object_expressions_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_recursion_guard_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp167_dogfood.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp141_cp163_content_model.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp137_cp140_frontmatter.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp131_cp136_project_model.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp130_eval_parity.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp129_eval_capabilities.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp127_eval_json_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp126_eval_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp124_json_object_wall.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp117_object_methods_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_final_language_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 benchmarks/hierarchy_scaling.py
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp172_from_entries_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp173_index_by_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp174_pick_omit_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp175_merge_deep_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp176_cp182_collection_algebra_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp183_collection_composition_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v43_cp184_cp186_sort_by_smoke.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 benchmarks/typed_content_scaling.py
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 benchmarks/project_dependency_scaling.py

# Fail-closed guard: every maintained workflow's official macOS matrix entry
# (arm64 -> macos-latest, x86-64 -> macos-26-intel) must agree on the runner,
# so packaging/release/installer/verification matrices cannot drift apart.
test-macos-runner-policy:
	python3 tests/macos_runner_policy_test.py


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

# @path on the tracked page `404` must emit root-absolute web paths because a
# 404 document is served at arbitrary request depth; checking is unchanged.
test-pathto-404: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/pathto_404_smoke.sh

# Tracked outputs deterministically preserve the source content file's
# permissions (executable scripts stay executable); rebuilds do not depend on
# the output's prior mode.
test-output-permissions: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/output_permissions_smoke.sh

# Unreadable content/@input/template sources must fail the build with a clear
# "not readable" diagnostic and preserve the previously successful output; an
# empty-but-readable source remains a distinct, valid state. Protects the
# ProjectInfo host read path from regressing a failed read into an empty file.
test-unreadable-source: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/unreadable_source_smoke.sh

# Modified-mode staleness must never return success for an immediate source
# edit: equal content/page-info mtimes (coarse filesystem timestamp resolution)
# are treated as potentially stale so the rebuild is always attempted.
test-incremental-modified-immediate: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/incremental_modified_immediate.sh

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

test-path-alias: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/path_alias_smoke.sh

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

test-init-lock: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/init_lock_smoke.sh


test-guarantee-registry:
	python3 scripts/check_guarantee_registry.py

# Single-repository CI variant: asserts everything a Nift-only checkout can
# prove and PASSes; sibling-dependent public-claim surface audit is deferred,
# never silently skipped into green.
test-guarantee-registry-ci:
	python3 scripts/check_guarantee_registry.py --local

test-test-integrity:
	python3 scripts/test_integrity_check.py tests scripts --output "$(TEST_DIR)/bh2/test-integrity-report.json"
	python3 scripts/check_header_name_collisions.py

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
	rm -f libnift_c.a libnift_c.so libnift_c.dylib bindings/go/embed-harness
	rm -rf dist/embed-prefix bindings/node/build bindings/python/build
	rm -f bindings/python/nift/_nift*.so
	rm -rf bindings/csharp/src/Nift/bin bindings/csharp/src/Nift/obj
	rm -rf bindings/csharp/apps/NiftEmbedHarness/bin bindings/csharp/apps/NiftEmbedHarness/obj
	rm -rf bindings/csharp/apps/NiftAspDogfood/bin bindings/csharp/apps/NiftAspDogfood/obj
	rm -rf bindings/csharp/tests/Nift.Tests/bin bindings/csharp/tests/Nift.Tests/obj
	rm -rf bindings/csharp/bench/bin bindings/csharp/bench/obj
	rm -rf bindings/python/__pycache__ bindings/python/nift/__pycache__ \
		bindings/python/tests/__pycache__ packaging/__pycache__
	find bindings/python packaging tests scripts -type f -name '*.pyc' -delete 2>/dev/null || true
	rm -rf tests/__pycache__ scripts/__pycache__ packaging/__pycache__
	$(MAKE) -C minifypp clean
	$(MAKE) -C jsonic clean

.PHONY: FORCE libffi-check san-libffi-check tsan-libffi-check test-ffi-abi test-libffi-source test-gate6ar-ffi test-libffi-static-archive test-libffi-dependencies test-libffi-private-symbols test-pic-depfiles test-node-package-licenses test-v45-adversarial-runtime test-v45-integration-dogfood test-v45-embed-contracts test-v45-embed-staged-consumer test-v45-concurrency test-v45-job-control test-v45-target test-v45-native-runtime embed go-binding csharp-binding node-binding python-binding bindings test-build-boundary test-embed test-go-binding test-csharp-binding test-node-binding test-python-binding test-bindings test-all test benchmark-memory-10k benchmark-10k test-tracking-scaling test-full-build-scaling test-recovery-epoch test-performance-scaling test-sanitize memory-safety-smoke all clean test-jsonic test-jsonic-sync test-markuppp-sync test-json test-json-schema test-runtime-value test-cp15-numeric-repair test-cp17-bytes test-cp18-bytes test-cp19-bytes test-cp21-bytes test-cp18-bytes-sanitize test-cp18-bytes-tsan test-console test-progress-render test-progress-pty test-snap-contract test-distribution-summary test-version-consistency test-diagnostics test-minify test-json-schema-integration test-markup-json-directives test-engine test-engine-bindings test-engine-loaders test-engine-source-read test-engine-pathto test-engine-concurrency test-engine-project test-engine-reload test-engine-pagination-snapshot test-c-abi test-c-abi-c-smoke test-host-seam benchmark-c-abi test-project-state test-project-host test-public-header test-conformance test-content test-commands test-comments test-ownership-concurrency test-zero-mutation test-repair-campaign test-pagination-ordering test-json-binding test-control-flow test-requirements test-path-alias test-path-safety test-metadata-safety test-template-optional test-contracts test-init-targets test-init-lock test-unreadable-source test-incremental-modified-immediate test-v41-certification test-v42-language test-v42-struct test-v43-language test-macos-runner-policy install uninstall


.PHONY: test-v45-concurrency-sanitize test-v45-concurrency-tsan

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

# CP28 v4.2 performance/memory certification: per-workload build timings and
# peak RSS (baseline/current interleaved for v4.1-only workloads) plus the
# 10k-page ordinary-project A/B audit. Requires --baseline and --current.
benchmark-cp28: $(TARGET)
	python3 benchmarks/cp28_bench.py --baseline "$(CP28_BASELINE)" --current "$(CURDIR)/$(TARGET)" --samples "$(CP28_SAMPLES:%=%)"
	python3 benchmarks/perf_regression_audit.py --baseline "$(CP28_BASELINE)" --current "$(CURDIR)/$(TARGET)" --pages 10000 --samples 20


$(TEST_DIR)/san/%.o: %.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(SAN_CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) -MMD -MP -c "$<" -o "$@"

$(TEST_DIR)/san/%.o: %.c
	mkdir -p "$(dir $@)"
	$(CC) -Imarkuppp/vendor/cmark -std=c99 -Wall -Wextra -pedantic $(SANITIZER_FLAGS) -MMD -MP -c "$<" -o "$@"

$(SAN_TARGET): $(SAN_OBJECTS) $(SAN_LIBFFI_A)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -std=c++17 -pthread $(SANITIZER_FLAGS) $(SAN_OBJECTS) $(SAN_LIBFFI_A) -o "$@"

test-sanitize: $(SAN_TARGET)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(SAN_TARGET)" --version

test-v45-concurrency-sanitize: $(SAN_TARGET)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(SAN_TARGET)" tests/v45_async.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(SAN_TARGET)" tests/v45_threads.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(SAN_TARGET)" tests/v45_mutex.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(SAN_TARGET)" tests/v45_atomics.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(SAN_TARGET)" tests/v45_adversarial_runtime.sh

CP18_BYTES_SAN_TEST := $(TEST_DIR)/nift-cp18-bytes-sanitize$(EXEEXT)
$(CP18_BYTES_SAN_TEST): tests/cp18_bytes.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A)
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(SAN_CPPFLAGS) $(LDFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) tests/cp18_bytes.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o "$@"

CP21_BYTES_SAN_TEST := $(TEST_DIR)/nift-cp21-bytes-sanitize$(EXEEXT)
$(CP21_BYTES_SAN_TEST): tests/cp21_bytes_embed.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A)
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(SAN_CPPFLAGS) $(LDFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) tests/cp21_bytes_embed.cpp $(filter-out $(TEST_DIR)/san/src/nift.o $(TEST_DIR)/san/src/CLI.o,$(SAN_OBJECTS)) $(SAN_LIBFFI_A) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o "$@"

test-cp18-bytes-sanitize: $(SAN_TARGET) $(CP18_BYTES_SAN_TEST) $(CP21_BYTES_SAN_TEST)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(CP18_BYTES_SAN_TEST)"
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(SAN_TARGET)" tests/cp17_bytes.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(SAN_TARGET)" tests/cp18_bytes.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(SAN_TARGET)" tests/cp19_bytes_io.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(SAN_TARGET)" tests/cp20_bytes_ffi.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(CP21_BYTES_SAN_TEST)"

test-pagination-sanitize: $(SAN_TARGET)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(SAN_TARGET)" tests/pagination_sanitizer_smoke.sh

$(TEST_DIR)/tsan/%.o: %.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(TSAN_CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(TSAN_FLAGS) -MMD -MP -c "$<" -o "$@"

$(TEST_DIR)/tsan/%.o: %.c
	mkdir -p "$(dir $@)"
	$(CC) -Imarkuppp/vendor/cmark -std=c99 -Wall -Wextra -pedantic $(TSAN_FLAGS) -MMD -MP -c "$<" -o "$@"

$(TSAN_TARGET): $(TSAN_OBJECTS) $(TSAN_LIBFFI_A)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -std=c++17 -pthread $(TSAN_FLAGS) $(TSAN_OBJECTS) $(TSAN_LIBFFI_A) -o "$@"

test-pagination-tsan: $(TSAN_TARGET)
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(TSAN_TARGET)" tests/pagination_sanitizer_smoke.sh

test-v45-concurrency-tsan: $(TSAN_TARGET)
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(TSAN_TARGET)" tests/v45_async.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(TSAN_TARGET)" tests/v45_threads.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(TSAN_TARGET)" tests/v45_mutex.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(TSAN_TARGET)" tests/v45_atomics.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(TSAN_TARGET)" tests/v45_adversarial_runtime.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(TSAN_TARGET)" bash tests/v46_import_worker_ownership_smoke.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 $(PYTHON) tests/v46_resource_paths.py "$(CURDIR)/$(TSAN_TARGET)"

CP18_BYTES_TSAN_TEST := $(TEST_DIR)/nift-cp18-bytes-tsan$(EXEEXT)
$(CP18_BYTES_TSAN_TEST): tests/cp18_bytes.cpp $(filter-out $(TEST_DIR)/tsan/src/nift.o $(TEST_DIR)/tsan/src/CLI.o,$(TSAN_OBJECTS)) $(TSAN_LIBFFI_A)
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(TSAN_CPPFLAGS) $(LDFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(TSAN_FLAGS) tests/cp18_bytes.cpp $(filter-out $(TEST_DIR)/tsan/src/nift.o $(TEST_DIR)/tsan/src/CLI.o,$(TSAN_OBJECTS)) $(TSAN_LIBFFI_A) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o "$@"

CP21_BYTES_TSAN_TEST := $(TEST_DIR)/nift-cp21-bytes-tsan$(EXEEXT)
$(CP21_BYTES_TSAN_TEST): tests/cp21_bytes_embed.cpp $(filter-out $(TEST_DIR)/tsan/src/nift.o $(TEST_DIR)/tsan/src/CLI.o,$(TSAN_OBJECTS)) $(TSAN_LIBFFI_A)
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(TSAN_CPPFLAGS) $(LDFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(TSAN_FLAGS) tests/cp21_bytes_embed.cpp $(filter-out $(TEST_DIR)/tsan/src/nift.o $(TEST_DIR)/tsan/src/CLI.o,$(TSAN_OBJECTS)) $(TSAN_LIBFFI_A) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o "$@"

test-cp18-bytes-tsan: $(TSAN_TARGET) $(CP18_BYTES_TSAN_TEST) $(CP21_BYTES_TSAN_TEST)
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 "$(CP18_BYTES_TSAN_TEST)"
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(TSAN_TARGET)" tests/cp17_bytes.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(TSAN_TARGET)" tests/cp18_bytes.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(TSAN_TARGET)" tests/cp19_bytes_io.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(TSAN_TARGET)" tests/cp20_bytes_ffi.sh
	env -u LD_PRELOAD TSAN_OPTIONS=halt_on_error=1 "$(CP21_BYTES_TSAN_TEST)"

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
	bash "$(CURDIR)/../jsonic/jsonic/tests/check_nift_sync_test.sh"
	bash "$(CURDIR)/../minify/minify/scripts/check-nift-sync.sh" "$(CURDIR)/minifypp"
	bash "$(CURDIR)/../minify/minify/tests/check_nift_sync_test.sh"

memory-safety-checkpoint-6-run: $(TARGET)
	mkdir -p .build/memory-safety
	python3 scripts/checkpoint6_integration.py --nift "$(CURDIR)/$(TARGET)" --rounds 60 --pages 90 --output .build/memory-safety/checkpoint-6-integration.json

memory-safety-checkpoint-6: memory-safety-checkpoint-6-sync memory-safety-checkpoint-6-run

memory-safety-checkpoint-6-sanitize: memory-safety-checkpoint-6-sync $(SAN_TARGET)
	mkdir -p .build/memory-safety
	env -u LD_PRELOAD python3 scripts/checkpoint6_integration.py --nift "$(CURDIR)/$(SAN_TARGET)" --rounds 12 --pages 30 --output .build/memory-safety/checkpoint-6-integration-sanitizer.json

valgrind-memory-safety-checkpoint-6: memory-safety-checkpoint-6-sync $(TARGET)
	mkdir -p .build/memory-safety
	python3 scripts/checkpoint6_integration.py --valgrind --nift "$(CURDIR)/$(TARGET)" --rounds 12 --pages 40 --output .build/memory-safety/checkpoint-6-valgrind.json

.PHONY: memory-safety-checkpoint-6-sync memory-safety-checkpoint-6-run memory-safety-checkpoint-6 memory-safety-checkpoint-6-sanitize valgrind-memory-safety-checkpoint-6

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

# Nift v4.4 execution/shell/packages campaign focused tests.
test-v44-execution-shell: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_execution_shell_smoke.sh


# Portable Python discovery for the AST fuzz/property target. Windows msys2
# PATH exposes python (from setup-python) rather than python3.
PYTHON ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null)
test-v44-language-foundation: $(TARGET) $(PARSER_STATEMENT_STATE_TEST)
	$(PARSER_STATEMENT_STATE_TEST)
	tests/v44_ast_expression_smoke.sh
	tests/v44_ast_constant_fold_smoke.sh
	tests/v44_ast_differential_corpus.sh
	tests/v44_root_path_corruption_reproducers.sh
	@if [ -n "$(PYTHON)" ]; then NIFT="$(CURDIR)/$(TARGET)" $(PYTHON) tests/v44_ast_fuzz.py; else echo "  (fuzz skipped: no python3/python on PATH)"; fi
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_perf_scaling_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_element_assignment_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp2_variadic_functions_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_struct_variadic_methods_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp3_variadic_lambdas_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp4_spread_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp5_generalized_native_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp6_cp7_glob_filesystem_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp8_structured_wildcards_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp9_map_markup_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp10_filesystem_authority_smoke.sh

# Remaining v4.4 shell/restriction/package surface. Kept as a separate target so
# the package tests can run with NIFT and the sibling sqlite package injected.
# autoconf exit 77 (platform skip) from a v44 test is an acknowledged skip,
# not a failure: POSIX-only tests report it on platforms without the facility.
V44_SKIP_77 := ; st=$$?; if [ $$st -eq 77 ]; then echo "  (skipped)"; else exit $$st; fi
PORTABLE_TIMEOUT ?= python3 -c "import subprocess,sys; subprocess.call(sys.argv[1:], timeout=120)"

test-v44-shell-restricted: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_shell_glob.sh $(V44_SKIP_77)
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp21_history_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp22_completion_smoke.sh
	@if command -v python3 >/dev/null 2>&1; then $(PORTABLE_TIMEOUT) python3 -u tests/v44_interactive_completion_pty.py $(CURDIR)/$(TARGET) $(V44_SKIP_77); else echo "  (skipped: python3 unavailable)"; fi
	@if command -v python3 >/dev/null 2>&1; then $(PORTABLE_TIMEOUT) python3 -u tests/v44_shell_foreground_tty_smoke.py $(CURDIR)/$(TARGET) $(V44_SKIP_77); else echo "  (skipped: python3 unavailable)"; fi
	bash tests/v44_cp27_restricted_smoke.sh $(CURDIR)/$(TARGET)

test-v44-packages: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/package_refs_smoke.sh $(V44_SKIP_77)
	NIFT="$(CURDIR)/$(TARGET)" tests/package_metadata_smoke.sh
	$(PYTHON) tests/package_transaction_smoke.py "$(CURDIR)/$(TARGET)" $(V44_SKIP_77)
	NIFT="$(CURDIR)/$(TARGET)" tests/package_callable_closure_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/package_hardening_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/package_module_export_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" SQLITE_PACKAGE="$(CURDIR)/../nift-packages/sqlite" tests/package_sqlite_dogfood.sh $(V44_SKIP_77)
	NIFT="$(CURDIR)/$(TARGET)" tests/package_combined_dogfood.sh $(V44_SKIP_77)
	NIFT="$(CURDIR)/$(TARGET)" tests/package_tools_dogfood.sh
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v44_relative_import_ownership_smoke.sh
	$(PYTHON) tests/v46_resource_paths.py "$(CURDIR)/$(TARGET)"

test-v44-automation: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_automation_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_script_comments_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_package_language_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_scalar_conversion_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_control_flow_else_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_struct_callable_escape_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_executable_script_smoke.sh

test-v44-hooks: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_hooks_smoke.sh

test-v44: test-v44-execution-shell test-v44-language-foundation test-v44-shell-restricted test-v44-packages test-v44-automation test-v44-hooks

# Nift v4.5 native runtime/shell campaign. This target grows checkpoint by checkpoint.
test-v45-invocation: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v45_cli_invocation_smoke.sh

.PHONY: test-v45-invocation

test-v45-host-introspection: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v45_host_introspection_smoke.sh

.PHONY: test-v45-host-introspection

test-v45-concurrency: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v45_threads.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v45_mutex.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v45_atomics.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v45_async.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v45_concurrency_hardening.sh
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v46_import_worker_ownership_smoke.sh

ifneq ($(OS),Windows_NT)
test-v45-job-control: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v45_jobs_background.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v45_job_control.sh
else
test-v45-job-control:
	@echo "test-v45-job-control: skipped (POSIX job control unavailable)"
endif

test-v45-target: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/v45_target.sh

test-v45-native-runtime: test-v45-invocation test-v45-host-introspection test-v45-integration-dogfood test-v45-adversarial-runtime \
	test-v45-concurrency test-v45-job-control test-v45-target

test-v46-time-cli: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v46_time_smoke.sh

test-v46-time: test-v46-time-cli test-v46-time-embed

.PHONY: test-v46-time test-v46-time-cli test-v46-time-embed

test-v46-timer: $(TARGET) $(V46_TIMER_UNIT_TEST) $(V46_TIME_EMBED_TEST)
	$(V46_TIMER_UNIT_TEST)
	NIFT="$(CURDIR)/$(TARGET)" tests/v46_timer_smoke.sh
	$(V46_TIME_EMBED_TEST)

test-v46-timer-sanitize: $(SAN_TARGET) $(V46_TIMER_UNIT_SAN_TEST) $(V46_TIMER_EMBED_SAN_TEST)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(SAN_TARGET)" tests/v46_timer_smoke.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(V46_TIMER_UNIT_SAN_TEST)"
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(V46_TIMER_EMBED_SAN_TEST)"

.PHONY: test-v46-timer test-v46-timer-sanitize

test-v46-secure-random-cli: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v46_secure_random_smoke.sh

test-v46-secure-random-embed: $(V46_SECURE_RANDOM_EMBED_TEST)
	$(V46_SECURE_RANDOM_EMBED_TEST)

test-v46-secure-random: test-v46-secure-random-cli test-v46-secure-random-embed

.PHONY: test-v46-secure-random test-v46-secure-random-cli test-v46-secure-random-embed

test-v46-output-cli: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v46_output_smoke.sh

test-v46-output-embed: $(V46_OUTPUT_EMBED_TEST)
	$(V46_OUTPUT_EMBED_TEST)

test-v46-output: test-v46-output-cli test-v46-output-embed

.PHONY: test-v46-output test-v46-output-cli test-v46-output-embed

test-v46-b4-cp1: $(TARGET) $(PARSER_STATEMENT_STATE_TEST) $(RUNTIME_VALUE_TEST) $(V46_OUTPUT_EMBED_TEST) \
	test-v42-language test-v43-language test-v44-execution-shell test-v44-packages \
	test-v44-language-foundation \
	test-json-schema-integration test-host-seam test-v45-ffi test-v45-concurrency \
	test-v45-embed-contracts test-c-abi test-c-abi-c-smoke test-bindings
	$(PARSER_STATEMENT_STATE_TEST)
	$(RUNTIME_VALUE_TEST)
	tests/v44_ast_expression_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" tests/v46_b4_cp1_characterization.sh
	$(V46_OUTPUT_EMBED_TEST)

.PHONY: test-v46-b4-cp1

test-v46-b4-cp2: test-v46-b4-cp1 $(DIAGNOSTIC_OUTCOME_TEST)
	$(DIAGNOSTIC_OUTCOME_TEST)

test-v46-b4-cp3: test-v46-b4-cp2 test-runtime-value $(CP3_EMBED_TEST)
	$(CP3_EMBED_TEST)
	bash tests/v46_b4_cp3_recoverable_errors.sh

test-v46-b4-pre-cp4: test-v46-b4-cp3
	bash tests/v46_b4_precp4_repairs.sh

test-v46-b4-cp4a: test-v46-b4-pre-cp4
	bash tests/v46_b4_cp4a_filesystem.sh

test-v46-b4-cp4b: test-v46-b4-cp4a
	bash tests/v46_b4_cp4b_streams.sh

test-v46-b4-cp4b-stream-operators: test-v46-b4-cp4b
	bash tests/v46_b4_cp4b_stream_operators.sh

test-v46-b4-cp4c: test-v46-b4-cp4b-stream-operators
	bash tests/v46_b4_cp4c_json_schema.sh

test-v46-b4-cp5a: test-v46-b4-cp4c
	bash tests/v46_b4_cp5a_ffi_recoverable.sh

test-v46-b4-cp6: test-v46-b4-cp5a
	bash tests/v46_b4_cp6_import_module_projection.sh

test-v46-b4-cp5b: test-v46-b4-cp6
	bash tests/v46_b4_cp5b_import_source_recoverable.sh

test-v46-b4-cp7: test-v46-b4-cp5b
	bash tests/v46_b4_cp7_worker_hardening.sh

test-v46-b4-cp8: test-v46-b4-cp7
	bash tests/v46_b4_cp8_embedding_abi.sh

test-v46-b4-cp9: test-v46-b4-cp8
	bash tests/v46_b4_cp9_final_certification.sh

test-v46-b5-cp10: test-v46-b4-cp9
	bash tests/v46_b5_cp10_lock_graph.sh

test-v46-b5-cp11: test-v46-b5-cp10
	bash tests/v46_b5_cp11_resolver.sh

test-v46-b5-cp12: test-v46-b5-cp11
	bash tests/v46_b5_cp12_graph_commands.sh

.PHONY: test-v46-b4-cp2 test-v46-b4-cp3 test-v46-b4-pre-cp4 test-v46-b4-cp4a test-v46-b4-cp4b test-v46-b4-cp4b-stream-operators test-v46-b4-cp4c test-v46-b4-cp5a test-v46-b4-cp6 test-v46-b4-cp5b test-v46-b4-cp7 test-v46-b4-cp8 test-v46-b4-cp9 test-v46-b5-cp10 test-v46-b5-cp11 test-v46-b5-cp12

test-v46-relative-imports: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v44_relative_import_ownership_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v46_import_worker_ownership_smoke.sh

test-v46-relative-imports-sanitize: $(SAN_TARGET)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(SAN_TARGET)" bash tests/v44_relative_import_ownership_smoke.sh
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT="$(CURDIR)/$(SAN_TARGET)" bash tests/v46_import_worker_ownership_smoke.sh

test-v46-resource-paths-embed: $(V46_RESOURCE_PATHS_EMBED_TEST)
	$(V46_RESOURCE_PATHS_EMBED_TEST)

test-v46-resource-paths: $(TARGET) $(V46_RESOURCE_PATHS_EMBED_TEST)
	$(PYTHON) tests/v46_resource_paths.py "$(CURDIR)/$(TARGET)"
	$(V46_RESOURCE_PATHS_EMBED_TEST)
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_cp22_completion_smoke.sh

test-v46-resource-paths-sanitize: $(SAN_TARGET) $(V46_RESOURCE_PATHS_EMBED_SAN_TEST)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(PYTHON) tests/v46_resource_paths.py "$(CURDIR)/$(SAN_TARGET)"
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(V46_RESOURCE_PATHS_EMBED_SAN_TEST)"

.PHONY: test-v46-relative-imports test-v46-relative-imports-sanitize test-v46-resource-paths test-v46-resource-paths-embed test-v46-resource-paths-sanitize

test-cp20-bytes: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/cp20_bytes_ffi.sh

.PHONY: test-cp20-bytes
