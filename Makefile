CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -pthread
CPPFLAGS ?= -Isrc -Iminifypp/include -Iminifypp/src
LDFLAGS ?=
LDLIBS ?=

SOURCES := src/nift.cpp src/CLI.cpp src/FileSystem.cpp src/JsonFile.cpp src/JsonSchema.cpp minifypp/src/Minify.cpp src/Parser.cpp src/ProjectInfo.cpp src/WatchList.cpp src/BuildProgress.cpp
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
MEMORY_SMOKE := $(TEST_DIR)/nift-memory-san$(EXEEXT)
JSON_TEST := $(TEST_DIR)/nift-json-smoke$(EXEEXT)
JSON_SCHEMA_TEST := $(TEST_DIR)/nift-json-schema-smoke$(EXEEXT)

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

test-minify:
	$(MAKE) -C minifypp test-smoke

test-json-schema-integration: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/json_schema_integration_smoke.sh

test-content: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/parser_content_smoke.sh

test-comments: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/comments_smoke.sh

test-json-binding: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/json_binding_smoke.sh

test-control-flow: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/control_flow_smoke.sh

test-pagination: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/pagination_smoke.sh

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

.PHONY: benchmark-memory-10k benchmark-10k test-tracking-scaling test-sanitize memory-safety-smoke all clean test-jsonic test-jsonic-sync test-json test-json-schema test-minify test-json-schema-integration test-content test-comments test-json-binding test-control-flow test-requirements test-path-safety test-metadata-safety test-template-optional test-contracts test-init-targets install uninstall


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
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(SAN_TARGET)" --version

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
