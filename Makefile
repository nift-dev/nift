CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -pthread
CPPFLAGS ?= -Isrc -Iinclude -Iminifypp/include -Iminifypp/src
LDFLAGS ?=
LDLIBS ?=

SOURCES := src/nift.cpp src/CLI.cpp src/Engine.cpp src/FileSystem.cpp src/JsonFile.cpp src/JsonSchema.cpp minifypp/src/Minify.cpp src/Parser.cpp src/ProjectInfo.cpp src/WatchList.cpp src/BuildProgress.cpp
OBJECTS := $(SOURCES:.cpp=.o)
DEPFILES := $(OBJECTS:.o=.d)

ifeq ($(OS),Windows_NT)
	EXEEXT := .exe
	PREFIX ?= $(LOCALAPPDATA)/Programs/Nift
	INSTALL_PROGRAM = cp
else
	EXEEXT :=
	PREFIX ?= /usr/local
	INSTALL_PROGRAM = install -m 0755
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

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPFILES)

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
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_smoke.cpp $(ENGINE_CORE_OBJECTS) $(LDLIBS) -o $@

test-engine: $(ENGINE_TEST)
	$(ENGINE_TEST)


test-comments: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/comments_smoke.sh

test-json-binding: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/json_binding_smoke.sh

test-control-flow: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/control_flow_smoke.sh

test-collections: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/collection_ops_smoke.sh

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
	$(MAKE) -C minifypp clean
	$(MAKE) -C jsonic clean

.PHONY: benchmark-memory-10k benchmark-10k test-tracking-scaling test-full-build-scaling test-recovery-epoch test-performance-scaling test-sanitize memory-safety-smoke all clean test-jsonic test-jsonic-sync test-json test-json-schema test-console test-diagnostics test-minify test-json-schema-integration test-engine test-content test-comments test-json-binding test-control-flow test-requirements test-path-safety test-metadata-safety test-template-optional test-contracts test-init-targets install uninstall


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
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) -c "$<" -o "$@"

$(SAN_TARGET): $(SAN_OBJECTS)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -std=c++17 -pthread $(SANITIZER_FLAGS) $(SAN_OBJECTS) -o "$@"

test-sanitize: $(SAN_TARGET)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(SAN_TARGET)" --version

test-pagination-sanitize: $(SAN_TARGET)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 NIFT_BIN="$(CURDIR)/$(SAN_TARGET)" tests/pagination_sanitizer_smoke.sh

$(TEST_DIR)/tsan/%.o: %.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(TSAN_FLAGS) -c "$<" -o "$@"

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
