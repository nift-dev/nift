CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude
BUILD_DIR := .build
SMOKE := $(BUILD_DIR)/jsonic-smoke
ADVERSARIAL := $(BUILD_DIR)/jsonic-adversarial
MEMORY_LIFETIME := $(BUILD_DIR)/jsonic-memory-lifetime
MEMORY_LIFETIME_SAN := $(BUILD_DIR)/jsonic-memory-lifetime-san
SANITIZER_FLAGS ?= -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined

all: test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SMOKE): tests/json_smoke.cpp include/json.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(ADVERSARIAL): tests/json_adversarial.cpp include/json.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(MEMORY_LIFETIME): tests/json_memory_lifetime.cpp include/json.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(MEMORY_LIFETIME_SAN): tests/json_memory_lifetime.cpp include/json.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic $(SANITIZER_FLAGS) $< -o $@

test-smoke: $(SMOKE)
	./$(SMOKE)

test-adversarial: $(ADVERSARIAL)
	./$(ADVERSARIAL)

test: test-smoke test-adversarial

test-sanitize:
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic $(SANITIZER_FLAGS) tests/json_smoke.cpp -o $(BUILD_DIR)/jsonic-smoke-san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD_DIR)/jsonic-smoke-san
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic $(SANITIZER_FLAGS) tests/json_adversarial.cpp -o $(BUILD_DIR)/jsonic-adversarial-san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD_DIR)/jsonic-adversarial-san

memory-safety-checkpoint-1a: $(MEMORY_LIFETIME) $(MEMORY_LIFETIME_SAN)
	mkdir -p $(BUILD_DIR)/memory-safety
	python3 scripts/memory_safety.py --project jsonic++ --mode sanitizer --output $(BUILD_DIR)/memory-safety/checkpoint-1a-sanitizer.json --iterations 1 --command './$(MEMORY_LIFETIME_SAN) --iterations 120'
	python3 scripts/memory_safety.py --project jsonic++ --mode rss --output $(BUILD_DIR)/memory-safety/checkpoint-1a-rss.json --iterations 1 --command './$(MEMORY_LIFETIME) --iterations 400'

valgrind-memory-safety-checkpoint-1a: $(MEMORY_LIFETIME)
	mkdir -p $(BUILD_DIR)/memory-safety
	python3 scripts/memory_safety.py --project jsonic++ --mode valgrind --output $(BUILD_DIR)/memory-safety/checkpoint-1a-valgrind.json --iterations 1 --command './$(MEMORY_LIFETIME) --iterations 40'

memory-safety-smoke:
	mkdir -p $(BUILD_DIR)/memory-safety
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic $(SANITIZER_FLAGS) tests/json_adversarial.cpp -o $(BUILD_DIR)/jsonic-memory-san
	python3 scripts/memory_safety.py --project jsonic++ --mode sanitizer --output $(BUILD_DIR)/memory-safety/checkpoint-0.json --iterations 2 --command './$(BUILD_DIR)/jsonic-memory-san'

check-nift-sync:
	@test -n "$(NIFT_DIR)" || (echo "NIFT_DIR=/path/to/nift is required" >&2; exit 2)
	./scripts/check-nift-sync.sh "$(NIFT_DIR)"

check-minify-sync:
	@test -n "$(MINIFY_DIR)" || (echo "MINIFY_DIR=/path/to/minify is required" >&2; exit 2)
	./scripts/check-minify-sync.sh "$(MINIFY_DIR)"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test test-smoke test-adversarial test-sanitize memory-safety-smoke memory-safety-checkpoint-1a valgrind-memory-safety-checkpoint-1a check-nift-sync check-minify-sync clean
