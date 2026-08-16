CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude -Isrc

TARGET := minify
LIBSRC := src/Minify.cpp
TESTDIR := .build
SMOKE := $(TESTDIR)/minifypp-smoke
FUZZ_SMOKE := $(TESTDIR)/minifypp-fuzz-smoke
SANITIZER_FLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
FUZZ_CASES ?= 10000
BENCH_REPETITIONS ?= 10000
BENCH_ITERATIONS ?= 20

all: $(TARGET)

$(TARGET): cli/main.cpp $(LIBSRC) include/minify/Minify.h src/Json.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) cli/main.cpp $(LIBSRC) -o $@

test: test-smoke test-node test-generated test-jsx test-css-semantics test-formats test-cross-format test-cli test-fuzz

check-nift-sync:
	@test -n "$(NIFT_MINIFYPP_DIR)" || { echo "set NIFT_MINIFYPP_DIR to Nift's minifypp directory" >&2; exit 2; }
	bash scripts/check-nift-sync.sh "$(NIFT_MINIFYPP_DIR)"

test-fuzz:
	mkdir -p $(TESTDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/fuzz_smoke.cpp $(LIBSRC) -o $(FUZZ_SMOKE)
	$(FUZZ_SMOKE) $(FUZZ_CASES)

test-sanitize:
	mkdir -p $(TESTDIR)
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic $(SANITIZER_FLAGS) tests/minify_smoke.cpp $(LIBSRC) -o $(TESTDIR)/minifypp-smoke-sanitize
	$(TESTDIR)/minifypp-smoke-sanitize
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic $(SANITIZER_FLAGS) tests/fuzz_smoke.cpp $(LIBSRC) -o $(TESTDIR)/minifypp-fuzz-sanitize
	$(TESTDIR)/minifypp-fuzz-sanitize $(FUZZ_CASES)
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic $(SANITIZER_FLAGS) cli/main.cpp $(LIBSRC) -o $(TESTDIR)/minify-sanitize
	MINIFY_BIN="$(CURDIR)/$(TESTDIR)/minify-sanitize" bash tests/cli_smoke.sh

benchmark:
	mkdir -p $(TESTDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) benchmarks/minify_benchmark.cpp $(LIBSRC) -o $(TESTDIR)/minifypp-benchmark
	@if command -v /usr/bin/time >/dev/null 2>&1; then \
	  /usr/bin/time -f 'peak_rss_kib,%M' $(TESTDIR)/minifypp-benchmark $(BENCH_REPETITIONS) $(BENCH_ITERATIONS); \
	else \
	  $(TESTDIR)/minifypp-benchmark $(BENCH_REPETITIONS) $(BENCH_ITERATIONS); \
	fi

distcheck:
	bash scripts/distcheck.sh

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

test-css-semantics:
	bash tests/minify_css_postcss_semantics.sh

test-cli: $(TARGET)
	bash tests/cli_smoke.sh

clean:
	rm -rf $(TESTDIR) $(TARGET)

.PHONY: all test check-nift-sync test-smoke test-node test-generated test-jsx test-css-semantics test-formats test-cross-format test-cli test-fuzz test-sanitize benchmark distcheck clean

test-formats:
	bash tests/minify_format_idempotence.sh


test-cross-format:
	bash tests/cross_format_adversarial.sh
