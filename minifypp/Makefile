CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude -Isrc

TARGET := minify
LIBSRC := src/Minify.cpp
TESTDIR := .build
SMOKE := $(TESTDIR)/minifypp-smoke

all: $(TARGET)

$(TARGET): cli/main.cpp $(LIBSRC) include/minify/Minify.h src/Json.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) cli/main.cpp $(LIBSRC) -o $@

test: test-smoke test-node test-generated test-jsx test-formats test-cross-format test-cli

check-nift-sync:
	@test -n "$(NIFT_MINIFYPP_DIR)" || { echo "set NIFT_MINIFYPP_DIR to Nift's minifypp directory" >&2; exit 2; }
	bash scripts/check-nift-sync.sh "$(NIFT_MINIFYPP_DIR)"

test-smoke:
	mkdir -p $(TESTDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/minify_smoke.cpp $(LIBSRC) -o $(SMOKE)
	$(SMOKE)

test-node:
	bash tests/minify_node_semantics.sh

test-generated:
	bash tests/minify_generated_semantics.sh

test-jsx:
	bash tests/minify_jsx_generated.sh

test-cli: $(TARGET)
	bash tests/cli_smoke.sh

clean:
	rm -rf $(TESTDIR) $(TARGET)

.PHONY: all test check-nift-sync test-smoke test-node test-generated test-jsx test-formats test-cross-format test-cli clean

test-formats:
	bash tests/minify_format_idempotence.sh


test-cross-format:
	bash tests/cross_format_adversarial.sh
