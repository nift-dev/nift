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

.PHONY: benchmark-memory-10k benchmark-10k test-tracking-scaling all clean test-jsonic test-jsonic-sync test-json test-json-schema test-minify test-json-schema-integration test-content test-comments test-json-binding test-control-flow test-requirements test-path-safety test-metadata-safety test-template-optional test-contracts install uninstall


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
