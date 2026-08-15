CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude -Isrc

TARGET := sift
LIBSRC := src/Sift.cpp
TESTDIR := .build
SMOKE := $(TESTDIR)/minify-smoke

all: $(TARGET)

$(TARGET): cli/main.cpp $(LIBSRC) include/sift/Sift.h src/Json.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) cli/main.cpp $(LIBSRC) -o $@

test: test-smoke test-node test-generated test-jsx test-formats test-cli

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

.PHONY: all test test-smoke test-node test-generated test-jsx test-formats test-cli clean

test-formats:
	bash tests/minify_format_idempotence.sh
