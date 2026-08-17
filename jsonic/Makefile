CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude
BUILD_DIR := .build
SMOKE := $(BUILD_DIR)/jsonic-smoke
ADVERSARIAL := $(BUILD_DIR)/jsonic-adversarial

all: test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SMOKE): tests/json_smoke.cpp include/json.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(ADVERSARIAL): tests/json_adversarial.cpp include/json.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

test-smoke: $(SMOKE)
	./$(SMOKE)

test-adversarial: $(ADVERSARIAL)
	./$(ADVERSARIAL)

test: test-smoke test-adversarial

test-sanitize:
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) -std=c++17 -O1 -g -Wall -Wextra -pedantic -fno-omit-frame-pointer -fsanitize=address,undefined tests/json_smoke.cpp -o $(BUILD_DIR)/jsonic-smoke-san
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/jsonic-smoke-san
	$(CXX) $(CPPFLAGS) -std=c++17 -O1 -g -Wall -Wextra -pedantic -fno-omit-frame-pointer -fsanitize=address,undefined tests/json_adversarial.cpp -o $(BUILD_DIR)/jsonic-adversarial-san
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/jsonic-adversarial-san

check-nift-sync:
	@test -n "$(NIFT_DIR)" || (echo "NIFT_DIR=/path/to/nift is required" >&2; exit 2)
	./scripts/check-nift-sync.sh "$(NIFT_DIR)"

check-minify-sync:
	@test -n "$(MINIFY_DIR)" || (echo "MINIFY_DIR=/path/to/minify is required" >&2; exit 2)
	./scripts/check-minify-sync.sh "$(MINIFY_DIR)"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test test-smoke test-adversarial test-sanitize check-nift-sync check-minify-sync clean
