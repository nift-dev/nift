CXX ?= g++
CC ?= cc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -pthread
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark
LDFLAGS ?=
LDLIBS ?=

# Shared core + CLI implementation (the ordinary Nift CLI needs only these).
PARSER_SOURCES := $(sort $(wildcard src/Parser*.cpp))
# Runtime 2.0 (src/rt): linked into nift from CP11 (opt-in VM, NIFT_VM=1).
RT_LINK_SOURCES := $(sort $(wildcard src/rt/*.cpp src/rt/natives/*.cpp))
# CP16: the legacy interpreter's Host ops and run() go through the Host layer
# (src/rt/host) and its process residues live on CP17's handle table.
RT_HOST_LINK_SOURCES := $(sort $(wildcard src/rt/host/*.cpp src/rt/handles/*.cpp))
CORE_SOURCES := src/nift.cpp src/ProjectOwnership.cpp src/PackageTransaction.cpp src/CLI.cpp src/shell/CommandDispatch.cpp src/Serve.cpp src/Process.cpp src/JobControl.cpp src/RuntimeValue.cpp src/Value.cpp src/FileSystem.cpp src/Sha256.cpp src/JsonFile.cpp src/JsonSchema.cpp minifypp/src/Minify.cpp markuppp/src/Markup.cpp markuppp/src/AsciiDoc.cpp markuppp/src/ReStructuredText.cpp $(PARSER_SOURCES) src/PageObserver.cpp src/ProjectInfo.cpp src/ProjectRead.cpp src/ProjectState.cpp src/WatchList.cpp src/BuildProgress.cpp src/Automation.cpp src/Hooks.cpp src/Ast.cpp $(RT_LINK_SOURCES) $(RT_HOST_LINK_SOURCES)
# Embedding-exclusive implementation (Engine, Context, C ABI). The reduced CLI
# never compiles or links these; they are built by the embed library and the
# engine/C ABI test targets.
EMBED_SOURCES := src/embed/Engine.cpp src/embed/Context.cpp src/embed/c_abi.cpp src/embed/program_api.cpp
SOURCES := $(CORE_SOURCES) $(EMBED_SOURCES)
MARKUP_C_NAMES := blocks buffer cmark cmark_ctype houdini_href_e houdini_html_e houdini_html_u html inlines iterator node references render scanners utf8
MARKUP_C_SOURCES := $(addprefix markuppp/vendor/cmark/,$(addsuffix .c,$(MARKUP_C_NAMES)))
MARKUP_C_OBJECTS := $(MARKUP_C_SOURCES:.c=.o)
OBJECTS := $(SOURCES:.cpp=.o) $(MARKUP_C_OBJECTS)
CORE_SOURCES += src/shell/Spawn.cpp
SOURCES += src/shell/Spawn.cpp
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
	# Winsock for the `nift serve` port probe (src/Serve.cpp).
	LDLIBS += -lws2_32
	SHARED_LIB := libnift_c.so
	# The shared library contains Serve.o too, so it needs Winsock as well.
	SHARED_SYS_LIBS := -lws2_32
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
# Vendored libffi is built with its own flags, kept separate from Nift's
# warning-disciplined CXXFLAGS. -Wno-deprecated-declarations silences the
# upstream legacy Java raw-API deprecation warnings emitted inside vendored
# libffi itself (java_raw_api.c calls its own deprecated entry points); there
# is no upstream configure switch that drops that translation unit. The
# suppression applies only to the libffi build, never to Nift's own sources.
LIBFFI_CFLAGS ?= -O2 -fPIC -Wno-deprecated-declarations
CPPFLAGS += -I$(LIBFFI_INCLUDE)
# The deep profile permits stack-slot reuse for the broad recursion corpus.
# The lifetime profile retains use-after-scope checking. Compatibility dispatch
# uses independent sanitizer frames so expression depth guards remain reachable.
SANITIZER_FLAGS ?= -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-address-use-after-scope
SAN_TARGET := $(TEST_DIR)/nift-sanitize$(EXEEXT)
SAN_OBJECTS := $(patsubst %.cpp,$(TEST_DIR)/san/%.o,$(SOURCES)) $(patsubst %.c,$(TEST_DIR)/san/%.o,$(MARKUP_C_SOURCES))
SAN_LIBFFI_BUILD := $(TEST_DIR)/libffi/sanitize-$(LIBFFI_TARGET)
SAN_LIBFFI_STAMP := $(SAN_LIBFFI_BUILD)/.nift-built
SAN_LIBFFI_A := $(SAN_LIBFFI_BUILD)/install/lib/libffi.a
SAN_LIBFFI_INCLUDE := $(SAN_LIBFFI_BUILD)/install/include
SAN_CPPFLAGS = $(filter-out -I$(LIBFFI_INCLUDE),$(CPPFLAGS)) -I$(SAN_LIBFFI_INCLUDE)
# Lifetime sanitizer retains stack use-after-scope instrumentation. It runs the
# targeted lifetime corpus plus the exact file/recursion boundary oracle.
# Keep the profiles distinct: deep recursion also exercises other runtime frames.
SAN_LIFETIME_FLAGS ?= -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
SAN_LIFETIME_TARGET := $(TEST_DIR)/nift-sanitize-lifetime$(EXEEXT)
SAN_LIFETIME_OBJECTS := $(patsubst %.cpp,$(TEST_DIR)/san-lifetime/%.o,$(SOURCES)) $(patsubst %.c,$(TEST_DIR)/san-lifetime/%.o,$(MARKUP_C_SOURCES))
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

# Toolchain/build signature the libffi stamp depends on. Regenerated cheaply
# on every make; the file is rewritten only when its content changes, so an
# unchanged compiler/target/flags do not invalidate the cached libffi build,
# while a toolchain or config change forces a rebuild.
# Each make writes its own temporary file ($@.tmp.<pid>): concurrent makes
# (make certify runs targets in parallel) used to share $@.tmp, and one's mv
# could find the other's file already moved (FLAKY test-contracts @f1334c7).
LIBFFI_SIGNATURE := $(LIBFFI_BUILD)/.nift-toolchain-signature
$(LIBFFI_SIGNATURE): FORCE
	@mkdir -p "$(LIBFFI_BUILD)"
	@t="$@.tmp.$$$$"; printf '%s\n' "$(CC)|$(CXX)|$(LIBFFI_TARGET)|$(LIBFFI_CFLAGS)|$$($(CC) --version 2>/dev/null | head -1)|$$($(CXX) --version 2>/dev/null | head -1)" > "$$t" && { if cmp -s "$@" "$$t"; then rm -f "$$t"; else mv -f "$$t" "$@"; fi; }

SAN_LIBFFI_SIGNATURE := $(SAN_LIBFFI_BUILD)/.nift-toolchain-signature
$(SAN_LIBFFI_SIGNATURE): FORCE
	@mkdir -p "$(SAN_LIBFFI_BUILD)"
	@t="$@.tmp.$$$$"; printf '%s\n' "$(CC)|$(CXX)|$(LIBFFI_TARGET)|$(LIBFFI_CFLAGS) $(SANITIZER_FLAGS)|$$($(CC) --version 2>/dev/null | head -1)|$$($(CXX) --version 2>/dev/null | head -1)" > "$$t" && { if cmp -s "$@" "$$t"; then rm -f "$$t"; else mv -f "$$t" "$@"; fi; }

TSAN_LIBFFI_SIGNATURE := $(TSAN_LIBFFI_BUILD)/.nift-toolchain-signature
$(TSAN_LIBFFI_SIGNATURE): FORCE
	@mkdir -p "$(TSAN_LIBFFI_BUILD)"
	@t="$@.tmp.$$$$"; printf '%s\n' "$(CC)|$(CXX)|$(LIBFFI_TARGET)|$(LIBFFI_CFLAGS) $(TSAN_FLAGS)|$$($(CC) --version 2>/dev/null | head -1)|$$($(CXX) --version 2>/dev/null | head -1)" > "$$t" && { if cmp -s "$@" "$$t"; then rm -f "$$t"; else mv -f "$$t" "$@"; fi; }

# The stamp is a real sentinel: libffi is bootstrapped only when the script,
# provenance, or toolchain signature changed, not on every make.
$(LIBFFI_STAMP): scripts/build_vendored_libffi.sh scripts/check_vendored_libffi.py third_party/libffi/NIFT-PROVENANCE.md $(LIBFFI_SIGNATURE)
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(LIBFFI_CFLAGS)" bash scripts/build_vendored_libffi.sh "$(LIBFFI_BUILD)"

$(LIBFFI_A): | $(LIBFFI_STAMP)

LDLIBS += $(LIBFFI_A)

$(PARSER_OBJECTS) $(patsubst %.cpp,$(TEST_DIR)/pic/%.o,$(PARSER_SOURCES)): | $(LIBFFI_STAMP)

$(SAN_LIBFFI_STAMP): scripts/build_vendored_libffi.sh scripts/check_vendored_libffi.py third_party/libffi/NIFT-PROVENANCE.md $(SAN_LIBFFI_SIGNATURE)
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(LIBFFI_CFLAGS) $(SANITIZER_FLAGS)" bash scripts/build_vendored_libffi.sh "$(SAN_LIBFFI_BUILD)"

$(SAN_LIBFFI_A): | $(SAN_LIBFFI_STAMP)
$(patsubst %.cpp,$(TEST_DIR)/san/%.o,$(PARSER_SOURCES)): | $(SAN_LIBFFI_STAMP)

$(TSAN_LIBFFI_STAMP): scripts/build_vendored_libffi.sh scripts/check_vendored_libffi.py third_party/libffi/NIFT-PROVENANCE.md $(TSAN_LIBFFI_SIGNATURE)
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(LIBFFI_CFLAGS) $(TSAN_FLAGS)" bash scripts/build_vendored_libffi.sh "$(TSAN_LIBFFI_BUILD)"

$(TSAN_LIBFFI_A): | $(TSAN_LIBFFI_STAMP)
$(patsubst %.cpp,$(TEST_DIR)/tsan/%.o,$(PARSER_SOURCES)): | $(TSAN_LIBFFI_STAMP)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

%.o: %.c
	$(CC) -Imarkuppp/vendor/cmark $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPFILES) $(patsubst %.o,%.d,$(SAN_OBJECTS) $(SAN_LIFETIME_OBJECTS) $(TSAN_OBJECTS))

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

# Runtime 2.0 CP09 value model (src/rt): unit, randomised differential/property
# tests against the legacy RuntimeValue, lint, sanitizer build and benchmarks.
# CP15: the natives also link the vendored SHA-256 and the JSON Schema validator
# (neither uses RuntimeValue).
RT_VALUE_SOURCES := $(wildcard src/rt/*.cpp src/rt/natives/*.cpp) src/Sha256.cpp src/JsonSchema.cpp
RT_VALUE_HEADERS := $(wildcard src/rt/*.h src/rt/natives/*.h src/rt/natives/*.inc) tests/rt/rt_test.h tests/rt/legacy_oracle.h
# CP16 host built-ins: VM natives call HostCall, and the shared helpers they
# reuse live outside src/rt. Added here so every RT unit that links the native
# table has the same symbols as nift itself.
RT_VALUE_SOURCES += $(sort $(wildcard src/rt/host/*.cpp) $(wildcard src/rt/handles/*.cpp)) src/ParserHelpers.cpp src/ParserHostHelpers.cpp src/Process.cpp src/FileSystem.cpp
RT_VALUE_DEPS := $(RT_VALUE_SOURCES) $(RT_VALUE_HEADERS) src/RuntimeValue.cpp src/RuntimeValue.h
RT_VALUE_UNIT := $(TEST_DIR)/rt/value-unit$(EXEEXT)
RT_VALUE_PROPERTY := $(TEST_DIR)/rt/value-property$(EXEEXT)
RT_VALUE_REGRESS := $(TEST_DIR)/rt/value-regress$(EXEEXT)
RT_VALUE_BENCH := $(TEST_DIR)/rt/value-bench$(EXEEXT)
RT_VALUE_ROUND2 := $(TEST_DIR)/rt/value-round2$(EXEEXT)
RT_VALUE_D040 := $(TEST_DIR)/rt/value-d040$(EXEEXT)
RT_VALUE_DESTROY_OOM := $(TEST_DIR)/rt/value-destroy-oom$(EXEEXT)
RT_VALUE_DESTROY_OOM_SMALL := $(TEST_DIR)/rt/value-destroy-oom-small$(EXEEXT)
RT_HEAP_ACCOUNT_OOM := $(TEST_DIR)/rt/heap-account-oom$(EXEEXT)
RT_ATOM_BUDGET := $(TEST_DIR)/rt/atom-budget$(EXEEXT)
RT_NATIVE_BUDGET := $(TEST_DIR)/rt/native-budget$(EXEEXT)
RT_HEAP_LIMIT_ZERO := $(TEST_DIR)/rt/heap-limit-zero$(EXEEXT)
RT_VALUE_CYCLE_BENCH := $(TEST_DIR)/rt/value-cycle-bench$(EXEEXT)
RT_VALUE_CYCLE_BENCH_OFF := $(TEST_DIR)/rt/value-cycle-bench-off$(EXEEXT)
RT_SANITIZE_FLAGS := -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all

$(RT_VALUE_UNIT): tests/rt/value_unit.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/value_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

$(RT_VALUE_PROPERTY): tests/rt/value_property.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/value_property.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

$(RT_VALUE_REGRESS): tests/rt/value_regress.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/value_regress.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

$(RT_VALUE_BENCH): tests/rt/value_bench.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/rt/value_bench.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

.PHONY: test-rt-value test-rt-lint test-rt-value-sanitize bench-rt-value
# RT_VALUE_SEED / RT_VALUE_SCALE vary the randomised suite (default seed is fixed).
$(RT_VALUE_ROUND2): tests/rt/value_round2.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/value_round2.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# D-040 value semantics (L7 step 2.1/2.7a): structs as COW values, nested
# copy-on-write model, same(), identity summary bit, cycle checker.
$(RT_VALUE_D040): tests/rt/value_d040.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/value_d040.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# Memory investigation C1: releasing a value needs no memory (failing
# operator new; permanent regression for the destroy_oom reproducer).
$(RT_VALUE_DESTROY_OOM): tests/rt/value_destroy_oom.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/value_destroy_oom.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"
# A one-slot in-frame buffer: small graphs reach the spill and the
# pointer-reversal fallback at every injected allocation failure.
$(RT_VALUE_DESTROY_OOM_SMALL): tests/rt/value_destroy_oom.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) -DNIFT_RT_DESTROY_LOCAL=1 tests/rt/value_destroy_oom.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# The exact side table never allocates; a full table reports over budget
# (one-slot build; reserves ~2 GiB of address space, never touched).
$(RT_HEAP_ACCOUNT_OOM): tests/rt/heap_account_oom.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) -DNIFT_RT_SATURATED_SLOTS=1 tests/rt/heap_account_oom.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# Memory investigation C2: interned atoms count against the heap budget.
$(RT_ATOM_BUDGET): tests/rt/atom_budget.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/atom_budget.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# Memory investigation C3: natives check the budget before allocating results.
$(RT_NATIVE_BUDGET): tests/rt/native_budget.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/native_budget.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# C3 follow-up: a heap limit of 0 means no room, never "disabled".
$(RT_HEAP_LIMIT_ZERO): tests/runtime2/memory/heap_limit_zero_repro.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/runtime2/memory/heap_limit_zero_repro.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# Cost of the D-040 cycle check isolated from the store: the same benchmark
# with the check and with it compiled out; the guard is on the difference.
$(RT_VALUE_CYCLE_BENCH): tests/rt/value_cycle_bench.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/rt/value_cycle_bench.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"
$(RT_VALUE_CYCLE_BENCH_OFF): tests/rt/value_cycle_bench.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_RT_NO_CYCLE_CHECK tests/rt/value_cycle_bench.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"
.PHONY: bench-rt-value-cycle test-rt-heap-saturation test-rt-memory-c1 test-rt-memory-c2
# Memory investigation C2 end to end: new atoms count against the budget.
test-rt-memory-c2: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/memory_c2.sh
# Memory investigation C1 end to end: running out of memory mid-script ends
# with the original error, never an abort in the release (workers only).
test-rt-memory-c1: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/memory_c1.sh
# Large values count at their real size against the VM heap budget (~1.5 GiB
# RSS; workers only, not part of `make test`).
test-rt-heap-saturation: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/heap_saturation.sh
bench-rt-value-cycle: $(RT_VALUE_CYCLE_BENCH) $(RT_VALUE_CYCLE_BENCH_OFF)
	python3 tests/rt/value_cycle_guard.py "$(RT_VALUE_CYCLE_BENCH)" "$(RT_VALUE_CYCLE_BENCH_OFF)"

test-rt-value: $(RT_VALUE_UNIT) $(RT_VALUE_REGRESS) $(RT_VALUE_PROPERTY) $(RT_VALUE_ROUND2) $(RT_VALUE_D040) $(RT_VALUE_DESTROY_OOM) $(RT_VALUE_DESTROY_OOM_SMALL) $(RT_HEAP_ACCOUNT_OOM) $(RT_ATOM_BUDGET) $(RT_NATIVE_BUDGET) $(RT_HEAP_LIMIT_ZERO)
	"$(RT_VALUE_UNIT)"
	"$(RT_VALUE_REGRESS)"
	"$(RT_VALUE_ROUND2)"
	"$(RT_VALUE_PROPERTY)" $${RT_VALUE_SEED:-20261017} $${RT_VALUE_SCALE:-1}
	"$(RT_VALUE_D040)" $${RT_VALUE_SEED:-20261020} $${RT_VALUE_SCALE:-1}
	"$(RT_VALUE_DESTROY_OOM)"
	"$(RT_VALUE_DESTROY_OOM_SMALL)"
	"$(RT_HEAP_ACCOUNT_OOM)"
	"$(RT_ATOM_BUDGET)"
	"$(RT_NATIVE_BUDGET)"
	"$(RT_HEAP_LIMIT_ZERO)"

SOURCE_MAP_UNIT := $(TEST_DIR)/rt/source-map
$(SOURCE_MAP_UNIT): tests/rt/source_map_unit.cpp $(RT_VALUE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) -DNIFT_SOURCEMAP_COUNTER -pthread \
		tests/rt/source_map_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

SOURCE_MAP_RETRY_UNIT := $(TEST_DIR)/rt/source-map-retry
$(SOURCE_MAP_RETRY_UNIT): tests/rt/source_map_retry_unit.cpp src/rt/source_map.cpp src/rt/source_map.h \
		src/rt/module_link.cpp src/rt/module_link.h
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) -ffunction-sections -fdata-sections -pthread \
		tests/rt/source_map_retry_unit.cpp src/rt/source_map.cpp src/rt/module_link.cpp \
		-Wl,--gc-sections -o "$@"

ERROR_OOM_UNIT := $(TEST_DIR)/rt/error-oom
$(ERROR_OOM_UNIT): tests/rt/error_oom_unit.cpp src/rt/error.cpp src/rt/error.h \
		src/rt/heap_account.cpp src/rt/value.cpp src/rt/string.cpp
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) \
		tests/rt/error_oom_unit.cpp src/rt/error.cpp src/rt/heap_account.cpp \
		src/rt/value.cpp src/rt/string.cpp -o "$@"

# CP13B Phase B exceptions: permanent gates (tests/runtime2/cp13b/README.md).
# Functional tier (in test-rt-vm): fixtures in legacy/current/future modes,
# future mode on the portable switch-dispatch build too, harness self-tests,
# nullable Error presentation on both builds, SourceMap and Error-OOM units.
# Sanitizer tier (in test-rt-vm-sanitize): SourceMap ASan and TSan, Error-OOM
# ASan/LSan/UBSan. Certification tier (own certify leaf): E1-E8 mutants,
# 1M-catch stress, Error lifecycle. certification_guard.sh requires each edge.
CP13B_SWITCH_NIFT := $(TEST_DIR)/cp13b/nift-switch$(EXEEXT)
# The switch object tracks its full header closure (-MMD) so it can never be
# linked stale; every other object is the main build's, tracked the same way.
$(TEST_DIR)/cp13b/vm-switch.o: src/rt/vm.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_RT_VM_SWITCH -MMD -MP -c src/rt/vm.cpp -o "$@"
-include $(TEST_DIR)/cp13b/vm-switch.d
$(CP13B_SWITCH_NIFT): $(filter-out src/rt/vm.o,$(CLI_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o "$@"
.PHONY: test-runtime2-cp13b test-runtime2-cp13b-mutants
test-runtime2-cp13b: $(TARGET) $(CP13B_SWITCH_NIFT)
	$(PYTHON) tests/runtime2/cp13b/run.py --nift "$(CURDIR)/$(TARGET)" --selftest
	$(PYTHON) tests/runtime2/cp13b/run.py --nift "$(CURDIR)/$(TARGET)" --trace-selftest
	$(PYTHON) tests/runtime2/cp13b/run.py --nift "$(CURDIR)/$(TARGET)" --mode legacy
	$(PYTHON) tests/runtime2/cp13b/run.py --nift "$(CURDIR)/$(TARGET)" --mode current
	$(PYTHON) tests/runtime2/cp13b/run.py --nift "$(CURDIR)/$(TARGET)" --mode future
	$(PYTHON) tests/runtime2/cp13b/run.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --mode future
	$(PYTHON) tests/runtime2/cp13b/adversarial/selftest.py
	$(PYTHON) tests/runtime2/cp13b/adversarial/mutant_build_selftest.py
	bash tests/runtime2/cp13b/harness_selftest.sh "$(CURDIR)/$(TARGET)"
	$(PYTHON) tests/runtime2/cp13b/nullable_error_presentation.py --nift "$(CURDIR)/$(TARGET)" --switch-nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)"
test-runtime2-cp13b-mutants: $(TARGET)
	PYTHON="$(PYTHON)" bash tests/runtime2/cp13b/mutation_gate.sh "$(CURDIR)/$(TARGET)"
test-rt-vm: test-runtime2-cp13b
test-rt-vm: test-rt-source-map
test-rt-vm: test-rt-error-oom
test-rt-vm-sanitize: test-rt-source-map-sanitize
test-rt-vm-sanitize: test-rt-source-map-tsan
test-rt-vm-sanitize: test-rt-error-oom-sanitize
certify-targets: test-runtime2-cp13b-mutants

.PHONY: test-rt-source-map test-rt-source-map-retry test-rt-source-map-sanitize test-rt-source-map-tsan test-rt-error-oom test-rt-error-oom-sanitize
test-rt-source-map: $(SOURCE_MAP_UNIT) $(SOURCE_MAP_RETRY_UNIT)
	"$(SOURCE_MAP_UNIT)"
	"$(SOURCE_MAP_RETRY_UNIT)"

test-rt-source-map-retry: $(SOURCE_MAP_RETRY_UNIT)
	"$(SOURCE_MAP_RETRY_UNIT)"

test-rt-error-oom: $(ERROR_OOM_UNIT)
	"$(ERROR_OOM_UNIT)"

test-rt-error-oom-sanitize:
	mkdir -p "$(TEST_DIR)/rt-san"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) -pthread \
		tests/rt/error_oom_unit.cpp src/rt/error.cpp src/rt/heap_account.cpp \
		src/rt/value.cpp src/rt/string.cpp -o "$(TEST_DIR)/rt-san/error-oom"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		timeout 300 "$(TEST_DIR)/rt-san/error-oom"

test-rt-source-map-sanitize:
	mkdir -p "$(TEST_DIR)/rt-san"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) -DNIFT_SOURCEMAP_COUNTER -pthread \
		tests/rt/source_map_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/source-map"
	ulimit -f 100000; timeout 600 "$(TEST_DIR)/rt-san/source-map"

test-rt-source-map-tsan:
	mkdir -p "$(TEST_DIR)/rt-tsan"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_HANDLE_TSAN_FLAGS) -DNIFT_SOURCEMAP_COUNTER \
		tests/rt/source_map_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-tsan/source-map"
	ulimit -f 100000; TSAN_OPTIONS=halt_on_error=1 timeout 900 "$(TEST_DIR)/rt-tsan/source-map"

test-rt-lint:
	python3 tools/rt-lint.py --self-test
	python3 tools/rt-lint.py src/rt

# ASan+UBSan build of the rt tests (GCC/Clang on Linux or macOS). ASan
# reserves its shadow memory up front, so this target must run without
# `ulimit -v` (it refuses to start under one); the address-space tests
# (value-d040 large_accounting, under ulimit -v) run in test-rt-value instead.
test-rt-value-sanitize:
	@if [ "$$(ulimit -v)" != unlimited ]; then \
		echo "test-rt-value-sanitize: run without ulimit -v (ASan shadow memory; found $$(ulimit -v) KiB)"; exit 1; fi
	mkdir -p "$(TEST_DIR)/rt-san"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/value_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/value-unit"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/value_property.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/value-property"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/value_regress.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/value-regress"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/value_round2.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/value-round2"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/value_d040.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/value-d040"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/value_destroy_oom.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/value-destroy-oom"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) -DNIFT_RT_DESTROY_LOCAL=1 tests/rt/value_destroy_oom.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/value-destroy-oom-small"
	"$(TEST_DIR)/rt-san/value-unit"
	"$(TEST_DIR)/rt-san/value-regress"
	"$(TEST_DIR)/rt-san/value-round2"
	# Reference safety is enforced: a reference held across a mutation must abort.
	for m in stale-array stale-object stale-string; do \
		if ASAN_OPTIONS=detect_leaks=0 "$(TEST_DIR)/rt-san/value-round2" $$m 2>"$(TEST_DIR)/rt-san/$$m.log"; then \
			echo "FAIL: stale reference ($$m) not detected"; exit 1; fi; \
		grep -q "heap-use-after-free" "$(TEST_DIR)/rt-san/$$m.log" || { echo "FAIL: $$m"; exit 1; }; \
		echo "PASS stale reference detected: $$m"; done
	"$(TEST_DIR)/rt-san/value-property" $${RT_VALUE_SEED:-20261017} $${RT_VALUE_SCALE:-1}
	"$(TEST_DIR)/rt-san/value-d040" $${RT_VALUE_SEED:-20261020} $${RT_VALUE_SCALE:-1} no-large
	"$(TEST_DIR)/rt-san/value-destroy-oom"
	"$(TEST_DIR)/rt-san/value-destroy-oom-small"

# Runtime 2.0 CP17 typed handles (src/rt/handle.*, src/rt/handles/): unit,
# concurrency stress (ASan/UBSan and TSan), callgrind guard, and the
# no-legacy-handle-strings gate for VM paths. Not linked into nift yet (CP16
# links src/rt/handles/*.cpp into nift through RT_HOST_LINK_SOURCES).
RT_HANDLE_SOURCES := src/rt/handle.cpp $(sort $(wildcard src/rt/handles/*.cpp)) src/rt/value.cpp src/rt/string.cpp \
	src/rt/heap_account.cpp
RT_HANDLE_DEPS := $(RT_HANDLE_SOURCES) src/rt/handle.h $(wildcard src/rt/handles/*.h) src/rt/value.h \
	tests/rt/rt_test.h tests/rt/handle_test_support.h tests/rt/handle_lifecycle_unit_more.inc \
	tests/rt/handle_lifecycle_review.inc
RT_HANDLE_UNIT_SOURCES := tests/rt/handle_table_unit.cpp tests/rt/handle_table_ownership.cpp
RT_HANDLE_UNIT := $(TEST_DIR)/rt/handle-unit$(EXEEXT)
RT_HANDLE_STRESS := $(TEST_DIR)/rt/handle-stress$(EXEEXT)
RT_HANDLE_LIFECYCLE := $(TEST_DIR)/rt/handle-lifecycle$(EXEEXT)
RT_HANDLE_BENCH := $(TEST_DIR)/rt/handle-bench$(EXEEXT)
RT_HANDLE_TSAN_FLAGS := -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=thread -pthread

$(RT_HANDLE_UNIT): $(RT_HANDLE_UNIT_SOURCES) $(RT_HANDLE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $(RT_HANDLE_UNIT_SOURCES) $(RT_HANDLE_SOURCES) -o "$@"

$(RT_HANDLE_STRESS): tests/rt/handle_table_stress.cpp $(RT_HANDLE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/handle_table_stress.cpp $(RT_HANDLE_SOURCES) -o "$@"

$(RT_HANDLE_LIFECYCLE): tests/rt/handle_lifecycle_unit.cpp $(RT_HANDLE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/handle_lifecycle_unit.cpp $(RT_HANDLE_SOURCES) -o "$@"

$(RT_HANDLE_BENCH): tests/rt/handle_table_bench.cpp $(RT_HANDLE_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/rt/handle_table_bench.cpp $(RT_HANDLE_SOURCES) -o "$@"

.PHONY: test-rt-handles test-rt-handles-sanitize test-rt-handles-tsan test-rt-handles-guards
# RT_HANDLE_STRESS_MS / RT_HANDLE_RACE_ROUNDS size the concurrency runs.
test-rt-handles: $(RT_HANDLE_UNIT) $(RT_HANDLE_STRESS) $(RT_HANDLE_LIFECYCLE)
	ulimit -f 100000; ulimit -v 4000000; timeout 300 "$(RT_HANDLE_UNIT)"
	ulimit -f 100000; ulimit -v 4000000; timeout 300 "$(RT_HANDLE_LIFECYCLE)"
	ulimit -f 100000; ulimit -v 4000000; timeout 300 "$(RT_HANDLE_STRESS)" $${RT_HANDLE_STRESS_MS:-2000} $${RT_HANDLE_RACE_ROUNDS:-500}
	python3 tests/runtime2/cp17/no_handle_strings.py --self-test
	python3 tests/runtime2/cp17/no_handle_strings.py src/rt
	python3 tools/rt-lint.py src/rt/handle.h src/rt/handle.cpp src/rt/handles
	# D-035: ownership transfer is host-internal; natives never reach it.
	! grep -n 'transfer *(' src/rt/handles/native_access.h src/rt/handles/native_access.cpp

test-rt-handles-sanitize:
	mkdir -p "$(TEST_DIR)/rt-san"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) -pthread $(RT_HANDLE_UNIT_SOURCES) $(RT_HANDLE_SOURCES) \
		-o "$(TEST_DIR)/rt-san/handle-unit"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) -pthread tests/rt/handle_table_stress.cpp $(RT_HANDLE_SOURCES) \
		-o "$(TEST_DIR)/rt-san/handle-stress"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) -pthread tests/rt/handle_lifecycle_unit.cpp $(RT_HANDLE_SOURCES) \
		-o "$(TEST_DIR)/rt-san/handle-lifecycle"
	ulimit -f 100000; timeout 600 "$(TEST_DIR)/rt-san/handle-unit"
	ulimit -f 100000; timeout 600 "$(TEST_DIR)/rt-san/handle-lifecycle"
	ulimit -f 100000; timeout 600 "$(TEST_DIR)/rt-san/handle-stress" $${RT_HANDLE_STRESS_MS:-2000} \
		$${RT_HANDLE_RACE_ROUNDS:-300}

test-rt-handles-tsan:
	mkdir -p "$(TEST_DIR)/rt-tsan"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_HANDLE_TSAN_FLAGS) tests/rt/handle_table_stress.cpp $(RT_HANDLE_SOURCES) \
		-o "$(TEST_DIR)/rt-tsan/handle-stress"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_HANDLE_TSAN_FLAGS) tests/rt/handle_lifecycle_unit.cpp $(RT_HANDLE_SOURCES) \
		-o "$(TEST_DIR)/rt-tsan/handle-lifecycle"
	ulimit -f 100000; TSAN_OPTIONS=halt_on_error=1 timeout 900 "$(TEST_DIR)/rt-tsan/handle-lifecycle"
	ulimit -f 100000; TSAN_OPTIONS=halt_on_error=1 timeout 900 "$(TEST_DIR)/rt-tsan/handle-stress" \
		$${RT_HANDLE_STRESS_MS:-3000} $${RT_HANDLE_RACE_ROUNDS:-300}

# D-031: every run is under ulimit -f and timeout; ulimit -v only where no
# sanitizer is used (ASan/TSan reserve terabytes of shadow address space).
# Callgrind instruction guards on create/resolve cost (+10 %); run via heavy.sh.
test-rt-handles-guards: $(RT_HANDLE_BENCH)
	python3 tests/runtime2/cp17/handle_guards.py "$(RT_HANDLE_BENCH)" --check tests/runtime2/cp17/handle_guards.json

# Runtime 2.0 CP11: compiler + register VM (src/rt), linked into nift behind
# NIFT_VM=1. Unit tests in-process; differential parity against the legacy
# engine (VM corpus expectations, generated programs, rt-diff with policies).
RT_VM_UNIT := $(TEST_DIR)/rt/vm-unit$(EXEEXT)
RT_VM_RUN := $(TEST_DIR)/rt/vm-run$(EXEEXT)
RT_VM_PREPARED := $(TEST_DIR)/rt/vm-prepared-parse$(EXEEXT)
RT_VM_DEPS := $(RT_VALUE_SOURCES) $(RT_VALUE_HEADERS) tests/rt/rt_vm_tool.h src/RuntimeValue.cpp

$(RT_VM_UNIT): tests/rt/vm_unit.cpp $(RT_VM_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/vm_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# The prepared-parser port against legacy's own parser (src/Ast.cpp).
$(RT_VM_PREPARED): tests/rt/vm_prepared_parse.cpp src/rt/prepared_parse.cpp src/rt/prepared_parse.h src/Ast.cpp src/Ast.h
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/vm_prepared_parse.cpp src/rt/prepared_parse.cpp src/Ast.cpp src/RuntimeValue.cpp -o "$@"

# CP12 phase B: globals below the compiler (opcodes, analysis, shared inference slot).
RT_VM_GLOBALS := $(TEST_DIR)/rt/vm-globals-unit$(EXEEXT)
$(RT_VM_GLOBALS): tests/rt/vm_globals_unit.cpp $(RT_VM_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/vm_globals_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# CP12 phase B: the module graph and loader (D-041; rt/module_graph.h).
RT_VM_MODULES := $(TEST_DIR)/rt/vm-modules-unit$(EXEEXT)
$(RT_VM_MODULES): tests/rt/vm_modules_unit.cpp $(RT_VM_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/vm_modules_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

$(RT_VM_RUN): tests/rt/vm_run.cpp $(RT_VM_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/vm_run.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

.PHONY: test-rt-vm test-rt-vm-sanitize bench-rt-vm
test-rt-vm: test-runtime2-vm-corpus-complete $(RT_VM_UNIT) $(RT_VM_GLOBALS) $(RT_VM_MODULES) $(RT_VM_RUN) $(RT_VM_PREPARED) $(TARGET) $(TEST_DIR)/rt-parse-dump$(EXEEXT)
	"$(RT_VM_UNIT)"
	"$(RT_VM_GLOBALS)"
	"$(RT_VM_MODULES)"
	"$(RT_VM_PREPARED)"
	python3 tools/rt-lint.py src/rt
	python3 tools/rt-vm/legacy_names.py --check
	python3 tests/runtime2/vm_parity.py --nift "$(CURDIR)/$(TARGET)" --gen $${RT_VM_GEN:-300} --seed $${RT_VM_SEED:-1} --rt-diff
	python3 tests/runtime2/vm_adversarial_timeout_selftest.py
	python3 tests/runtime2/vm_adversarial.py --nift "$(CURDIR)/$(TARGET)" --seed $${RT_VM_SEED:-1} --count $${RT_VM_ADV:-600}
	python3 tests/runtime2/vm_subset.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_subset_gen.py --nift "$(CURDIR)/$(TARGET)" --suite both --seed $${RT_VM_SEED:-1} \
		--count $${RT_VM_SUBSET_GEN:-500}
	python3 tests/runtime2/vm_builtins.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_host.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_records_coverage.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_path_wording.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_known_refused.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_engine_selection.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_former_replay.py --nift "$(CURDIR)/$(TARGET)" --seed $${RT_VM_SEED:-1} \
		--sample $${RT_VM_FORMER:-600}
	python3 tests/runtime2/vm_d018.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_context_consistency.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_policy_negative.py
	python3 tests/runtime2/vm_review_cp12.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_golden.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_int_bounds.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_inventory.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_chain.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_modules.py --nift "$(CURDIR)/$(TARGET)" --gen $${RT_VM_MODULES_GEN:-100} --seed $${RT_VM_SEED:-1}
	python3 tests/runtime2/module_host_order.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_structs.py --nift "$(CURDIR)/$(TARGET)" --gen $${RT_VM_STRUCTS_GEN:-100} --seed $${RT_VM_SEED:-1}
	python3 tests/runtime2/vm_globals.py --nift "$(CURDIR)/$(TARGET)" --gen $${RT_VM_GLOBALS_GEN:-200} --seed $${RT_VM_SEED:-1}
	python3 tests/runtime2/semantics_modules.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/semantics_modules.py --nift "$(CURDIR)/$(TARGET)" --dir semantics-structs
	python3 tests/runtime2/modules_d041.py --nift "$(CURDIR)/$(TARGET)" --legacy-today
	python3 tools/rt-vm/cp12b_inventory.py selftest

# ASan+UBSan: the unit tests, then the corpus and generated programs on a
# sanitized nift (both engines in one sanitized binary).
test-rt-vm-sanitize: $(SAN_TARGET) $(TEST_DIR)/rt-parse-dump$(EXEEXT)
	mkdir -p "$(TEST_DIR)/rt-san"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/vm_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/vm-unit"
	env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(TEST_DIR)/rt-san/vm-unit"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/vm_globals_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/vm-globals-unit"
	env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(TEST_DIR)/rt-san/vm-globals-unit"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/vm_modules_unit.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/vm-modules-unit"
	env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(TEST_DIR)/rt-san/vm-modules-unit"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/vm_prepared_parse.cpp src/rt/prepared_parse.cpp src/Ast.cpp src/RuntimeValue.cpp -o "$(TEST_DIR)/rt-san/vm-prepared-parse"
	env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(TEST_DIR)/rt-san/vm-prepared-parse" 20000
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_parity.py --nift "$(CURDIR)/$(SAN_TARGET)" --gen $${RT_VM_GEN:-150} --seed $${RT_VM_SEED:-2} --timeout 120
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_adversarial.py --nift "$(CURDIR)/$(SAN_TARGET)" --seed $${RT_VM_SEED:-2} \
		--count $${RT_VM_ADV:-60} --per-family 250 --timeout 120
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_subset.py --nift "$(CURDIR)/$(SAN_TARGET)"
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_subset_gen.py --nift "$(CURDIR)/$(SAN_TARGET)" --suite both \
		--seed $${RT_VM_SEED:-2} --count $${RT_VM_SUBSET_GEN:-80} --timeout 120
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_builtins.py --nift "$(CURDIR)/$(SAN_TARGET)" --timeout 300
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_host.py --nift "$(CURDIR)/$(SAN_TARGET)"
	python3 tests/runtime2/vm_records_coverage.py --nift "$(CURDIR)/$(SAN_TARGET)" --timeout 300
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_path_wording.py --nift "$(CURDIR)/$(SAN_TARGET)" --timeout 300
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_engine_selection.py --nift "$(CURDIR)/$(SAN_TARGET)"
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_former_replay.py --nift "$(CURDIR)/$(SAN_TARGET)" --seed $${RT_VM_SEED:-2} \
		--sample $${RT_VM_FORMER:-80} --timeout 120
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_d018.py --nift "$(CURDIR)/$(SAN_TARGET)"
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_context_consistency.py --nift "$(CURDIR)/$(SAN_TARGET)"
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_review_cp12.py --nift "$(CURDIR)/$(SAN_TARGET)" --timeout 120
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_golden.py --nift "$(CURDIR)/$(SAN_TARGET)" --timeout 300
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_int_bounds.py --nift "$(CURDIR)/$(SAN_TARGET)"
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		python3 tests/runtime2/vm_chain.py --nift "$(CURDIR)/$(SAN_TARGET)"

# Review CP12 round 2: live mutation gate. One extra link of nift with the VM
# compiled -DNIFT_VM_MUTANTS (deliberate bugs selected by NIFT_VM_MUTANT=n,
# src/rt/vm_mutants.h); the differential gates must reject every mutant and
# pass the unmutated binary. Normal builds contain no mutant code.
RT_VM_MUTANTS := $(TEST_DIR)/nift-mutants$(EXEEXT)
RT_VM_MUTANT_OBJECTS := $(TEST_DIR)/mutants/vm.o $(TEST_DIR)/mutants/vm_ops.o $(TEST_DIR)/mutants/compiler_call.o \
	$(TEST_DIR)/mutants/prepared_outer.o
RT_VM_MUTATED := src/rt/vm.o src/rt/vm_ops.o src/rt/compiler_call.o src/rt/prepared_outer.o
$(TEST_DIR)/mutants/%.o: src/rt/%.cpp $(RT_VALUE_HEADERS)
	mkdir -p "$(TEST_DIR)/mutants"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_VM_MUTANTS -c $< -o $@
$(RT_VM_MUTANTS): $(filter-out $(RT_VM_MUTATED),$(CLI_OBJECTS)) $(RT_VM_MUTANT_OBJECTS) $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out $(RT_VM_MUTATED),$(CLI_OBJECTS)) $(RT_VM_MUTANT_OBJECTS) \
		$(LDLIBS) -o $@

.PHONY: test-rt-vm-mutants
test-rt-vm-mutants: $(RT_VM_MUTANTS) $(TEST_DIR)/rt-parse-dump$(EXEEXT) $(TARGET)
	python3 tests/runtime2/vm_mutation_gate.py --nift "$(CURDIR)/$(RT_VM_MUTANTS)" --rt-diff --jobs "$${NIFT_MUTANT_JOBS:-$$(( ($$(nproc) + 1) / 2 ))}"

# CP16 Host layer (src/rt/host): Host-op interface, trace v1/v2, run()
# options, process model, CP17 binding. Linked into nift (RT_HOST_LINK_SOURCES, CP16 legacy routing). Process
# tests run contained (D-031: ulimit -f/-v, timeout, own session, cleanup).
RT_HOST_SOURCES := $(sort $(wildcard src/rt/host/*.cpp))
# The process binding uses CP17's handle table (RT_HANDLE_SOURCES).
RT_HOST_SOURCES += $(RT_HANDLE_SOURCES)
RT_HOST_HEADERS := $(wildcard src/rt/host/*.h) $(wildcard src/rt/handles/*.h) src/rt/handle.h src/rt/value.h \
	src/HostTrace.h src/ProcessPOSIX.h src/ProcessEnvironment.h tests/rt/rt_test.h
RT_HOST_LAYER_UNIT := $(TEST_DIR)/rt/host-layer-unit$(EXEEXT)
RT_HOST_PROCESS_UNIT := $(TEST_DIR)/rt/host-process-unit$(EXEEXT)
$(RT_HOST_LAYER_UNIT): tests/rt/host_layer_unit.cpp $(RT_HOST_SOURCES) $(RT_HOST_HEADERS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/host_layer_unit.cpp $(RT_HOST_SOURCES) -o "$@"
$(RT_HOST_PROCESS_UNIT): tests/rt/host_process_unit.cpp $(RT_HOST_SOURCES) $(RT_HOST_HEADERS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt -DNIFT_HOST_TEST_HOOKS $(CXXFLAGS) tests/rt/host_process_unit.cpp $(RT_HOST_SOURCES) -o "$@"

# CP20 F6 cleanup (Linux /proc and process-group semantics): positive
# lifecycle controls in the functional Host target, the five source mutants in
# the mutation target, the ASan/UBSan positive cases in the sanitizer target.
# certification_guard.sh requires each edge (removal control).
.PHONY: test-runtime2-f6-cleanup test-runtime2-f6-mutants test-runtime2-f6-sanitize
F6_LINUX_ONLY = if [ "$$(uname -s)" != Linux ]; then echo "SKIP F6 cleanup controls: Linux-only"; exit 0; fi;
test-runtime2-f6-cleanup:
	@$(F6_LINUX_ONLY) $(PYTHON) tests/runtime2/f6_cleanup_controls.py
test-runtime2-f6-mutants:
	@$(F6_LINUX_ONLY) $(PYTHON) tests/runtime2/f6_cleanup_controls.py --mutants
test-runtime2-f6-sanitize:
	@$(F6_LINUX_ONLY) $(PYTHON) tests/runtime2/f6_cleanup_controls.py --sanitize
test-rt-host: test-runtime2-f6-cleanup
test-rt-host-mutants: test-runtime2-f6-mutants
test-rt-host-sanitize: test-runtime2-f6-sanitize

.PHONY: test-rt-host test-rt-host-sanitize test-rt-host-tsan
test-rt-host: $(RT_HOST_LAYER_UNIT) $(RT_HOST_PROCESS_UNIT)
	"$(RT_HOST_LAYER_UNIT)"
	tests/runtime2/cp16/run_contained.sh "$(RT_HOST_PROCESS_UNIT)"
	tests/runtime2/cp16/run_contained.sh "$(RT_HOST_PROCESS_UNIT)" managed_pgid_cleanup
# Contained and fast (~20 s); part of `make test` (D-034).
test: test-rt-host

# The binding builds compile src/rt/host-sources.txt instead of the whole src/rt;
# it must name exactly RT_HOST_LINK_SOURCES plus PageObserver and the handle
# table's src/rt support objects.
RT_HOST_LIST_EXPECTED := src/PageObserver.cpp src/rt/handle.cpp src/rt/value.cpp src/rt/string.cpp src/rt/heap_account.cpp $(RT_HOST_LINK_SOURCES)
RT_HOST_NATIVE_UNIT := $(TEST_DIR)/rt/host-native-unit$(EXEEXT)
$(RT_HOST_NATIVE_UNIT): tests/rt/host_observer_native.cpp $(RT_VALUE_SOURCES)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/host_observer_native.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"

# CP16 N1: the positive per-call Host-code tests and their harness controls.
.PHONY: test-runtime2-cp16-n1
test-runtime2-cp16-n1: $(TARGET) $(RT_HOST_NATIVE_UNIT)
	"$(RT_HOST_NATIVE_UNIT)"
	$(PYTHON) tests/runtime2/cp16_n1.py --nift "$(CURDIR)/$(TARGET)"
	$(PYTHON) tests/runtime2/cp16_n1_controls.py --nift "$(CURDIR)/$(TARGET)"
	$(PYTHON) tests/runtime2/cp16_n1_consumer_controls.py
	bash tests/runtime2/certification_guard_selftest.sh
test-rt-vm: test-runtime2-cp16-n1

.PHONY: test-rt-host-mutants
test-rt-host-mutants: $(TARGET)
	bash tests/runtime2/host_mutants.sh "$(CURDIR)/$(TARGET)"

.PHONY: test-runtime2-host-memory
test-runtime2-host-memory: $(TARGET)
	python3 tests/runtime2/vm_host_memory.py --nift "$(CURDIR)/$(TARGET)"

.PHONY: test-runtime2-module-host-order-mutant
test-runtime2-module-host-order-mutant:
	bash tests/runtime2/module_host_order_mutant.sh

.PHONY: test-rt-host-sources-list
test-rt-host-sources-list:
	@expected="$$(printf '%s\n' $(RT_HOST_LIST_EXPECTED) | LC_ALL=C sort)"; \
	actual="$$(grep -v '^#' src/rt/host-sources.txt | grep . | LC_ALL=C sort)"; \
	if [ "$$expected" != "$$actual" ]; then \
	  echo "src/rt/host-sources.txt is out of date:"; \
	  echo "--- expected"; printf '%s\n' "$$expected"; echo "--- listed"; printf '%s\n' "$$actual"; exit 1; \
	fi; echo "test-rt-host-sources-list: OK"
test: test-rt-host-sources-list

# ThreadSanitizer on the process tests (groups are shared with the host loop thread).
test-rt-host-tsan:
	mkdir -p "$(TEST_DIR)/rt-tsan"
	$(CXX) $(CPPFLAGS) -Itests/rt -DNIFT_HOST_TEST_HOOKS -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=thread -pthread \
		tests/rt/host_process_unit.cpp $(RT_HOST_SOURCES) -o "$(TEST_DIR)/rt-tsan/host-process-unit"
	env TSAN_OPTIONS=halt_on_error=1 tests/runtime2/cp16/run_contained.sh --sanitized "$(TEST_DIR)/rt-tsan/host-process-unit"

# ASan+UBSan on both Host-layer unit tests (no ulimit -v: the sanitizers
# reserve large shadow mappings; file size, time and cleanup still apply).
test-rt-host-sanitize:
	mkdir -p "$(TEST_DIR)/rt-san"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) -pthread tests/rt/host_layer_unit.cpp $(RT_HOST_SOURCES) -o "$(TEST_DIR)/rt-san/host-layer-unit"
	env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$(TEST_DIR)/rt-san/host-layer-unit"
	$(CXX) $(CPPFLAGS) -Itests/rt -DNIFT_HOST_TEST_HOOKS $(RT_SANITIZE_FLAGS) -pthread tests/rt/host_process_unit.cpp $(RT_HOST_SOURCES) -o "$(TEST_DIR)/rt-san/host-process-unit"
	env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		tests/runtime2/cp16/run_contained.sh --sanitized "$(TEST_DIR)/rt-san/host-process-unit"
	env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		tests/runtime2/cp16/run_contained.sh --sanitized "$(TEST_DIR)/rt-san/host-process-unit" managed_pgid_cleanup

# CP11 comparison: v4.12 baseline vs legacy vs VM vs CPython (needs valgrind).
bench-rt-vm: $(TARGET) $(RT_VM_RUN)
	python3 benchmarks/runtime2/vm_compare.py --nift "$(CURDIR)/$(TARGET)" --baseline "$${NIFT_BASELINE:-../baseline-src/nift}" \
		--vm-run "$(CURDIR)/$(RT_VM_RUN)"

# Callgrind instruction counts, rt::Value vs legacy RuntimeValue (needs valgrind).
bench-rt-value: $(RT_VALUE_BENCH)
	python3 tests/rt/value_bench_guard.py --self-test tests/rt/value_bench_baseline.json
	tools/rt-bench.sh "$(RT_VALUE_BENCH)" | tee "$(TEST_DIR)/rt/value-bench-table.md"
	python3 tests/rt/value_bench_guard.py "$(TEST_DIR)/rt/value-bench-table.md" tests/rt/value_bench_baseline.json

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

# First-party zero-warning gate. Every Nift-owned translation unit under src/
# (including src/embed/) must compile clean with the release warning set
# promoted to errors (-Wall -Wextra -pedantic -Werror). Embedded third-party
# tree (minifypp/, markuppp/, jsonic/, vendored libffi and cmark) follows its
# own upstream warning policy and is intentionally excluded from this gate.
# The default run uses the configured compiler and adds a Clang pass when
# clang++ is available; hosted release certification runs both passes
# explicitly. This gate is release-blocking: a candidate with first-party
# compiler warnings must never receive a release GO.
WARN_SOURCES := $(filter src/%,$(SOURCES))

.PHONY: test-warnings warnings-check
test-warnings:
	@$(MAKE) warnings-check
	@if command -v clang++ >/dev/null 2>&1; then echo "== clang++ first-party warnings pass =="; $(MAKE) warnings-check CXX=clang++; else echo "clang++ not available; Clang warnings pass skipped locally"; fi

warnings-check: $(LIBFFI_A)
	@set -e; \
	for f in $(WARN_SOURCES); do \
	  echo "  $(CXX): $$f"; \
	  $(CXX) $(CXXFLAGS) -Werror $(CPPFLAGS) -fsyntax-only "$$f"; \
	done
	@echo "PASS: zero first-party compiler warnings ($(CXX))"

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
PACKAGE_GRAPH_QUERY_TEST := $(TEST_DIR)/package-graph-query-unit$(EXEEXT)

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

$(PACKAGE_GRAPH_QUERY_TEST): tests/package_graph_query_unit.cpp src/PackageGraphQuery.h src/PackageGraphLock.h src/PackageMetadata.h src/FileSystem.cpp src/JsonFile.cpp
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/package_graph_query_unit.cpp src/FileSystem.cpp src/JsonFile.cpp -o $@

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

# libffi's members are re-added under an "ffi_" prefix: `ar r` replaces any
# member with the same basename, so an engine object such as
# src/rt/handles/types.o would otherwise silently overwrite libffi's types.o
# (ffi_type_* definitions) and break static consumers.
libnift_c.a: $(C_ABI_CORE) $(LIBFFI_A)
	rm -rf $@ $@.ffi && mkdir -p $@.ffi
	cd $@.ffi && ar x $(abspath $(LIBFFI_A)) && for m in *.o; do mv "$$m" "ffi_$$m"; done
	ar rcs $@ $(C_ABI_CORE) $@.ffi/ffi_*.o
	rm -rf $@.ffi

libnift_c.so: $(C_ABI_PIC) $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) -shared $(LIBFFI_SHARED_LINK_FLAGS) -o $@ $(C_ABI_PIC) $(LIBFFI_A) $(SHARED_SYS_LIBS)

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
	cd bindings/python && python3 -m unittest tests.test_nift tests.test_program

test-binding-warnings:
	python3 scripts/check_binding_warnings.py

test-bindings: test-binding-warnings test-go-binding test-csharp-binding test-node-binding test-python-binding

# The build-boundary gate is NON-DESTRUCTIVE (it runs in a temporary clean
# source tree, never in the caller's checkout), so it is safe under parallel
# Make; prerequisite order carries no sequencing meaning.
test-all: test test-embed test-bindings test-build-boundary test-host-seam test-conformance test-runtime2-rt-diff-selftest

# Load-sensitive measurements (docs/runtime-2/certification.md): wall-clock
# guards whose thresholds are unchanged and which must run with no competing
# workload. Plain `make test-all` runs them as before. With
# NIFT_DEFER_LOAD_SENSITIVE=1 (CI's parallel `make -j2 test-all`) the recipe
# lines prefixed with $(LOAD_SENSITIVE_DEFER) only print a "deferred" line, and
# CI runs `make test-load-sensitive` alone in a separate step.
ifeq ($(NIFT_DEFER_LOAD_SENSITIVE),1)
LOAD_SENSITIVE_DEFER = @echo "deferred (load-sensitive, run exclusively by make test-load-sensitive):"
else
LOAD_SENSITIVE_DEFER =
endif
# Exactly the wall-clock scripts, serially, regardless of NIFT_DEFER_LOAD_SENSITIVE.
.PHONY: test-load-sensitive
test-load-sensitive: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/v44_perf_scaling_smoke.sh
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_map_set_scaling_guard.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/process_capture.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/scripting_perf_guard.sh

# Plain `make test` = the ordinary Nift/CLI regression surface (C++ toolchain
# only). Embedding and binding suites are run through the focused targets.
test: test-runtime2-certification-guard test-build-recovery test-rt-value test-rt-lint test-runtime2-process-capture test-runtime2-vm-corpus test-runtime2-modified-race test-runtime2-dynamic-reads test-runtime2-fs-freedom test-v49-json-preflight-guard test-v49-glob-key-guard test-v49-glob-order test-v49-selector-parity test-parser-hardening test-content test-commands test-comments test-contracts test-json test-runtime-value test-cp15-numeric-repair test-cp17-bytes test-cp18-bytes test-cp19-bytes test-cp20-bytes test-cp21-bytes test-runtime2-rt-diff \
	test-json-schema test-console test-diagnostics test-minify \
	test-json-schema-integration test-markup-json-directives test-pagination test-pagination-ordering \
	test-template-optional test-requirements test-path-alias test-path-safety test-metadata-safety \
	test-init-targets test-init-lock test-init-migration test-init-transformations test-control-flow test-template-variables test-cross-feature test-v41-certification test-v42-language test-v42-struct test-v43-language test-config-validation \
	test-zero-mutation test-repair-campaign test-ownership-concurrency \
	test-macos-runner-policy \
	test-v44-execution-shell test-v44-language-foundation test-v44-shell-restricted \
	test-prepared-method-parity \
	test-v48-warn \
	test-v48-bytes-index-parity \
	test-v48-comparison-parity \
	test-v48-callable-parity \
	test-v48-map-set-index test-v48-map-set-scaling \
	test-v48-prepared-object-literal-parity test-v48-prepared-object-literal-guard \
	test-v48-prepared-collection-parity test-v48-prepared-collection-guard \
	test-location-method-receiver \
	test-v48-identifier-parity test-v48-identifier-guard \
	test-v48-callback-parity test-v48-callback-guard \
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


test-filesystem-type: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/filesystem_type_smoke.sh

test-process-empty-arg: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/process_empty_arg_smoke.sh

test-core-language-correctness: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" tests/core_language_correctness.sh

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
test-parser-hardening: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v47_deep_expression_smoke.sh

test-prepared-method-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v472_prepared_method_parity.sh

test-v48-warn: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_warn_smoke.sh

test-v48-bytes-index-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_bytes_index_parity.sh

test-v48-comparison-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_comparison_parity.sh

test-v48-callable-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_callable_parity.sh

test-v48-map-set-index: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_map_set_index_smoke.sh

test-v48-map-set-scaling: $(TARGET)
	$(LOAD_SENSITIVE_DEFER) NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_map_set_scaling_guard.sh

test-v48-prepared-object-literal-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_prepared_object_literal_parity.sh

test-v48-prepared-object-literal-guard: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_prepared_object_literal_guard.sh

test-v48-prepared-collection-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_prepared_collection_parity.sh

test-v48-prepared-collection-guard: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_prepared_collection_guard.sh

.PHONY: test-location-method-receiver
test-location-method-receiver: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/location_method_receiver.sh

.PHONY: test-v48-identifier-parity test-v48-identifier-guard
test-v48-identifier-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_identifier_parity.sh

test-v48-identifier-guard: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" python3 tests/v48_identifier_guard.py

.PHONY: test-v48-callback-parity test-v48-callback-guard
test-v48-callback-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v48_callback_parity.sh
	NIFT="$(CURDIR)/$(TARGET)" python3 tests/v48_callback_numeric_parity.py

# Only this object contains counters; ordinary CLI/library/ABI layouts and
# output have no instrumentation. Reuse the other ordinary CLI objects.
$(TEST_DIR)/lambda-cache/ParserExpression.o: src/ParserExpression.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_LAMBDA_CACHE_STATS -MMD -MP -c $< -o $@

-include $(TEST_DIR)/lambda-cache/ParserExpression.d
LAMBDA_CACHE_GUARD := $(TEST_DIR)/nift-lambda-cache-guard$(EXEEXT)
$(LAMBDA_CACHE_GUARD): $(filter-out src/ParserExpression.o,$(CLI_OBJECTS)) $(TEST_DIR)/lambda-cache/ParserExpression.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/ParserExpression.o,$(CLI_OBJECTS)) $(TEST_DIR)/lambda-cache/ParserExpression.o $(LDLIBS) -o $@

test-v48-callback-guard: $(LAMBDA_CACHE_GUARD)
	NIFT="$(CURDIR)/$(LAMBDA_CACHE_GUARD)" python3 tests/v48_callback_guard.py

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
	NIFT="$(CURDIR)/$(TARGET)" bash tests/v46_prepared_map_smoke.sh

test-commands: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/commands_smoke.sh

# `nift serve` smoke: full on Linux, lifecycle core on macOS with the
# Linux-only checks reported as SKIPPED (needs python3, binds loopback ports).
# Exit 77 (unsupported platform, or no python3) is the project's honest skip
# and is reported as skipped, never as a pass. Windows:
# tests/serve_windows_lifecycle.ps1 (pwsh).
test-serve: $(TARGET)
	@NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/serve_smoke.sh; st=$$?; \
	  if [ $$st -eq 77 ]; then echo "test-serve: SKIPPED (unsupported platform or python3 unavailable; nothing was tested)"; \
	  else exit $$st; fi
.PHONY: test-serve
test: test-serve

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

# Runtime 2.0 CP06: template scanner output parity against a reference
# binary (set NIFT_REF=/path/to/reference/nift; skipped when unset).
test-runtime2-scanner-parity: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" NIFT_REF="$(NIFT_REF)" bash tests/runtime2/scanner_parity.sh

# Runtime 2.0 CP02 (review defect D2) and CP03 (decision D-002).
test-runtime2-dynamic-reads: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/dynamic_reads.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/dynamic_reads_review.sh

test-runtime2-fs-freedom: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/fs_freedom.sh

.PHONY: test-runtime2-cp03-simplify
test-runtime2-cp03-simplify: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/cp01/missing_tracer_caller.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/cp03_missing_tracer.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/cp03_simplify.sh
test: test-runtime2-cp03-simplify

# feature-sha256: native SHA-256 (src/Sha256.*, vendored subset in
# third_party/bitcoin-sha256). Vendored-block check, module unit tests (normal
# and ASan+UBSan, each against the portable and, when the CPU has it, the
# x86 SHA-NI transform) and language/build tests of sha256()/sha256_file().
SHA256_DEPS := src/Sha256.cpp src/Sha256.h third_party/bitcoin-sha256/sha256_bitcoin.h
SHA256_UNIT := $(TEST_DIR)/sha256-unit$(EXEEXT)
SHA256_UNIT_SAN := $(TEST_DIR)/sha256-unit-san$(EXEEXT)
# Portable-only build (the configuration non-x86 targets get).
SHA256_UNIT_PORTABLE := $(TEST_DIR)/sha256-unit-portable$(EXEEXT)
SHA256_BENCH := $(TEST_DIR)/sha256-bench$(EXEEXT)
$(SHA256_UNIT): tests/sha256_unit.cpp $(SHA256_DEPS)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -Isrc $(CXXFLAGS) tests/sha256_unit.cpp src/Sha256.cpp -o "$@"
$(SHA256_UNIT_SAN): tests/sha256_unit.cpp $(SHA256_DEPS)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -Isrc -std=c++17 -pthread $(SANITIZER_FLAGS) -fno-sanitize-recover=undefined tests/sha256_unit.cpp src/Sha256.cpp -o "$@"
$(SHA256_UNIT_PORTABLE): tests/sha256_unit.cpp $(SHA256_DEPS)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -Isrc $(CXXFLAGS) -DNIFT_SHA256_DISABLE_X86_SHANI tests/sha256_unit.cpp src/Sha256.cpp -o "$@"
$(SHA256_BENCH): tests/sha256_bench.cpp $(SHA256_DEPS)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -Isrc $(CXXFLAGS) tests/sha256_bench.cpp src/Sha256.cpp -o "$@"
.PHONY: test-sha256 benchmark-sha256
# MinGW has no ASan/UBSan runtime: Windows runs the unsanitized builds only.
ifeq ($(OS),Windows_NT)
SHA256_UNIT_SANITIZED :=
else
SHA256_UNIT_SANITIZED := $(SHA256_UNIT_SAN)
endif
test-sha256: $(SHA256_UNIT) $(SHA256_UNIT_SANITIZED) $(SHA256_UNIT_PORTABLE) $(TARGET)
	python3 scripts/check_vendored_sha256.py
	"$(SHA256_UNIT)"
	$(if $(SHA256_UNIT_SANITIZED),"$(SHA256_UNIT_SANITIZED)")
	"$(SHA256_UNIT_PORTABLE)"
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/sha256_builtins.sh
test: test-sha256

# feature-host-deps (D-028): every read API records its dependency (incl.
# absence and unreadable paths), file().copy/move without project-root
# containment, bounded whole reads (NIFT_READ_LIMIT, fatal limit error). See
# docs/runtime-2/checkpoints/feature-host-deps.md. read_limit.py runs its
# memory cases under RLIMIT_AS.
READ_LIMIT_UNIT := $(TEST_DIR)/read-limit-unit$(EXEEXT)
$(READ_LIMIT_UNIT): tests/read_limit_unit.cpp src/ReadLimit.h
	mkdir -p "$(TEST_DIR)"
	$(CXX) -Isrc $(CXXFLAGS) tests/read_limit_unit.cpp -o "$@"
.PHONY: test-runtime2-host-deps
test-runtime2-host-deps: $(READ_LIMIT_UNIT) $(TARGET)
	"$(READ_LIMIT_UNIT)"
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/runtime2/host_deps.py
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/file_copy_move_freedom.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/build_write_ownership.sh
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/runtime2/read_limit.py
test: test-runtime2-host-deps

# feature-site-dx: website-generator DX fixes (build --auto resilience,
# unterminated literals, try/catch locations, run() launched, template
# warnings, strict values). See docs/runtime-2/checkpoints/feature-site-dx.md.
.PHONY: test-site-dx
test-site-dx: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/site_dx_smoke.sh
test: test-site-dx
# Throughput evidence (not a test): native module alone, in memory and from
# warm files. See docs/runtime-2/checkpoints/feature-sha256.md.
benchmark-sha256: $(SHA256_BENCH)
	"$(SHA256_BENCH)"

# D-010: the certification target. A checkpoint is certified locally with
# `make certify` (scripts/certify.py runs certify-targets: test-all plus every
# suite the CI workflows run, isolated and classified);
# certification_guard.sh keeps certify-targets complete.
CERTIFY_CI_TARGETS := bh2-test-integrity test-build-boundary-nondestructive test-complexity-invariants \
	test-crash-recovery test-filesystem-boundary test-filesystem-type test-guarantee-registry-ci \
	test-incremental-state-transitions test-init-functional-truth test-ownership-unit \
	test-pagination-equivalence test-parser-value-composition test-v44-automation test-v44-hooks \
	test-v44-packages test-v45-native-runtime checkpoint-10-cross-platform test-load-sensitive \
	test-cp19-s1-dispatch-unit
# Runtime 2.0 gates outside `make test`: the CP14 builtins suites, the
# sanitizer builds and the CP16 Host negative controls (owner, 2026-10-23:
# certification must include every required target, mutation check and
# sanitizer; the local precert checks this list against the plan).
CERTIFY_RUNTIME2_GATES := test-rt-builtins test-rt-builtins-sanitize test-rt-host-mutants test-rt-host-sanitize \
	test-rt-value-sanitize test-rt-vm-sanitize test-runtime2-host-memory test-runtime2-inventory-host-mutant \
	test-runtime2-module-host-order-mutant
.PHONY: certify certify-targets
# The certified set: test-all plus every suite the CI workflows run, plus the
# Runtime 2.0 gates above.
certify-targets: test-all $(CERTIFY_CI_TARGETS) $(CERTIFY_RUNTIME2_GATES)
# Isolated, classified run of that set (docs/runtime-2/certification.md).
certify:
	python3 scripts/certify.py --jobs "$(or $(CERTIFY_JOBS),$(shell nproc))"

# feature-build-recovery (D-020): ordinary failures never need --repair;
# SIGKILL at every named epoch point recovers automatically to a clean build.
test-build-recovery: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/build_recovery.py
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/build_recovery_random.py

# D-010: test-all must keep reaching the comprehensive suites.
test-runtime2-certification-guard: test-runtime2-vm-corpus-complete
	bash tests/runtime2/certification_guard.sh

# Golden-corpus completeness (existence only; never generates outputs): every
# vm-corpus/semantics .f has its .stdout/.stderr/.exit; the selftest removes a
# golden from a copy and must fail naming it. Seconds; before certification.
.PHONY: test-runtime2-vm-corpus-complete
test-runtime2-vm-corpus-complete:
	bash tests/runtime2/vm_corpus_complete.sh
	bash tests/runtime2/vm_corpus_complete.sh --selftest

# Runtime 2.0 CP16: legacy run()/cmd().run() and Host reads through the Host
# layer (C1-C3, unchanged result objects, trace v2). Contained (D-031).
.PHONY: test-runtime2-cp16-legacy
test-runtime2-cp16-legacy: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/cp16/legacy_routing.sh
test: test-runtime2-cp16-legacy
# Owner requirement (CP16): dependency recording is the same whichever engine
# starts the read, in every context; legacy recordings pinned per context.
.PHONY: test-runtime2-cp16-engine-parity
test-runtime2-cp16-engine-parity: $(TARGET)
	python3 tests/runtime2/cp16/engine_parity.py --nift "$(CURDIR)/$(TARGET)" --expected tests/runtime2/cp16/parity-expected
test: test-runtime2-cp16-engine-parity

# Runtime 2.0 CP07: process capture parity and no leaked capture files.
test-runtime2-process-capture: $(TARGET)
	$(LOAD_SENSITIVE_DEFER) NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/process_capture.sh

# Runtime 2.0 VM regression corpus (golden outputs from the canonical evaluator).
test-runtime2-vm-corpus: test-runtime2-vm-corpus-complete $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/vm_corpus.sh

# CP13 diagnostic corpus (docs/runtime-2/checkpoints/cp13-diagnostics-design.md):
# the 89-case baseline (byte-identical legacy text), the old-behaviour side of
# every approved change (changes/, decisions in changes/MANIFEST.tsv) and the
# message-to-code table check. The NEW side (--suite changes --expect new
# --vm) and the zero-false-rejection gate join once the CP13 VM and
# prevalidator land; until then these are not in the certification guard's
# REQUIRED list.
test-runtime2-diagnostics: $(TARGET)
	bash tests/runtime2/diagnostics/run.sh --binary "$(CURDIR)/$(TARGET)"
	bash tests/runtime2/diagnostics/run.sh --binary "$(CURDIR)/$(TARGET)" --suite changes --expect old
	bash tests/runtime2/diagnostics/run.sh --binary "$(CURDIR)/$(TARGET)" --suite changes --expect new
	python3 tests/runtime2/diagnostics/codes_check.py
	python3 tests/runtime2/diagnostics/json_check.py

# Syntax prevalidation (D-022 #3): the rules decided against legacy, and the
# permanent zero-false-rejection gate over every corpus and the vendored
# real-project units (every rejection must cite a grammar/lexical rule and be
# reviewed). Not yet in the certification guard's REQUIRED list.
test-runtime2-prevalidate-gate: $(TARGET) $(TEST_DIR)/rt-parse-dump$(EXEEXT)
	python3 tests/runtime2/prevalidate/rules_test.py --tool $(TEST_DIR)/rt-parse-dump$(EXEEXT) --legacy "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/prevalidate/gate.py --nift "$(CURDIR)/$(TARGET)" --tool $(TEST_DIR)/rt-parse-dump$(EXEEXT)
	python3 tests/runtime2/prevalidate/positive_test.py --nift "$(CURDIR)/$(TARGET)" --tool $(TEST_DIR)/rt-parse-dump$(EXEEXT)

# Runtime 2.0 CP08: differential harness self-check (A = B = this binary) over
# the committed corpora, scripts extracted from the smoke tests, and a seeded
# generated sample. Any difference here means nondeterminism or a harness bug.
RT_DIFF_CORPUS = --corpus tests/runtime2/rt-diff-corpus --corpus tests/runtime2/vm-corpus --corpus tests/runtime2/semantics \
	--corpus docs/evidence/cp410-competitiveness/probes --corpus tests/conformance/cases \
	--corpus bindings/python/app --corpus bindings/node/app --corpus bindings/csharp/apps
# Same binary on both sides: every channel must agree (determinism of the
# corpus, D-010 gate). RT_DIFF_GEN/RT_DIFF_SEED widen the generated share.
test-runtime2-rt-diff: $(TARGET)
	python3 tools/rt-diff/rt-diff --a "$(CURDIR)/$(TARGET)" --b "$(CURDIR)/$(TARGET)" $(RT_DIFF_CORPUS) \
		--extract-smoke tests --ignore tools/rt-diff/ignore.txt --gen $${RT_DIFF_GEN:-200} --seed $${RT_DIFF_SEED:-1} \
		--min-entries 600

# The harness catches seeded differences on every channel, hides only what its
# normalisations document, and its policies approve only exact differences.
test-runtime2-rt-diff-selftest: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/rt_diff_selftest.sh

# Runtime 2.0 CP01 (review defect D1): modified-mode timestamp races.
test-runtime2-modified-race: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" NIFT_RACE_CYCLES=$${NIFT_RACE_CYCLES:-300} bash tests/runtime2/modified_race.sh

.PHONY: test-runtime2-cp01-simplify
test-runtime2-cp01-simplify: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/runtime2/cp01_simplify.sh
test: test-runtime2-cp01-simplify

# `nift init --handover` writes a project-root HANDOVER.md byte-for-byte
# identical to the canonical copy (tests/fixtures/HANDOVER.md). Plain init
# must not create it, and it must never land under the output directory.
test-init-handover: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/init_handover_smoke.sh

# `nift init --migration` creates MIGRATION.md + HANDOVER.md + AGENTS.md (managed
# block) + investigation/ with deterministic existing-file policies and
# idempotent/malformed-safe AGENTS augmentation.
test-init-migration: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" tests/init_migration_smoke.sh

test-init-rewrite: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/init_transformation_smoke.py --mode rewrite

test-init-redesign: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/init_transformation_smoke.py --mode redesign

test-init-transformations: test-init-rewrite test-init-redesign
	python3 scripts/gen_migration_content.py --check

.PHONY: test-init-rewrite test-init-redesign test-init-transformations

# Network-gated: the vendored canonical handover and migration fixtures must
# match the live downloads. Skipped unless NIFT_LIVE_TESTS=1.
test-handover-live:
	tests/handover_live_check.sh

test-migration-live:
	tests/migration_live_check.sh

test-canonical-live: test-handover-live test-migration-live

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

# The CLI statically links vendored third-party code (libffi, the Bitcoin Core
# SHA-256 subset); install its notices with it (the snap is built this way).
LICENSEDIR ?= $(PREFIX)/share/licenses/nift
install: $(TARGET)
	mkdir -p "$(DESTDIR)$(BINDIR)"
	$(INSTALL_PROGRAM) "$(TARGET)" "$(DESTDIR)$(BINDIR)/$(TARGET)"
	mkdir -p "$(DESTDIR)$(LICENSEDIR)"
	cp LICENSE THIRD_PARTY_NOTICES.md "$(DESTDIR)$(LICENSEDIR)/"
	@echo "Installed $(TARGET) to $(DESTDIR)$(BINDIR)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f "$(DESTDIR)$(LICENSEDIR)/LICENSE" "$(DESTDIR)$(LICENSEDIR)/THIRD_PARTY_NOTICES.md"
	@echo "Removed $(DESTDIR)$(BINDIR)/$(TARGET)"

clean:
	rm -f $(OBJECTS) $(DEPFILES) "$(TARGET)"
	# Ordinary development clean removes Nift build products but PRESERVES the
	# reusable vendored libffi dependency cache so clean+make does not re-pay
	# the configure/build/install bootstrap. Use `make distclean` (or
	# `make pristine`) for a fully cold state. Genuine removal failures are
	# reported; concurrent `make` + `make clean` on one checkout is explicitly
	# unsupported.
	@if [ -d "$(TEST_DIR)" ]; then \
		find "$(TEST_DIR)" -mindepth 1 -maxdepth 1 ! -name libffi -exec rm -rf {} +; \
		leftovers="$$(find "$(TEST_DIR)" -mindepth 1 -maxdepth 1 ! -name libffi 2>/dev/null)"; \
		if [ -n "$$leftovers" ]; then echo "clean: could not remove: $$leftovers" >&2; exit 1; fi; \
		if [ ! -d "$(TEST_DIR)/libffi" ]; then rmdir "$(TEST_DIR)"; fi; \
	fi
	rm -f libnift_c.a libnift_c.so libnift_c.dylib bindings/go/embed-harness
	rm -rf dist/embed-prefix bindings/node/build bindings/python/build
	rm -f bindings/python/nift/_nift*.so
	rm -rf bindings/csharp/src/Nift/bin bindings/csharp/src/Nift/obj
	rm -rf bindings/csharp/apps/NiftEmbedHarness/bin bindings/csharp/apps/NiftEmbedHarness/obj
	rm -rf bindings/csharp/apps/NiftAspDogfood/bin bindings/csharp/apps/NiftAspDogfood/obj
	rm -rf bindings/csharp/tests/Nift.Tests/bin bindings/csharp/tests/Nift.Tests/obj
	rm -rf bindings/csharp/bench/bin bindings/csharp/bench/obj

# Fully cold state: remove .build entirely, including the vendored libffi
# dependency cache, so the next build re-bootstraps libffi from scratch.
distclean: clean
	rm -rf "$(TEST_DIR)"
	rm -f $(OBJECTS) $(DEPFILES) "$(TARGET)"

# Alias for distclean.
pristine: distclean
	rm -rf bindings/python/__pycache__ bindings/python/nift/__pycache__ \
		bindings/python/tests/__pycache__ packaging/__pycache__
	find bindings/python packaging tests scripts -type f -name '*.pyc' -delete 2>/dev/null || true
	rm -rf tests/__pycache__ scripts/__pycache__ packaging/__pycache__
	$(MAKE) -C minifypp clean
	$(MAKE) -C jsonic clean

.PHONY: FORCE test-build-recovery test-scripting-perf test-ffi-abi test-libffi-source test-gate6ar-ffi test-libffi-static-archive test-libffi-dependencies test-libffi-private-symbols test-pic-depfiles test-node-package-licenses test-v45-adversarial-runtime test-v45-integration-dogfood test-v45-embed-contracts test-v45-embed-staged-consumer test-v45-concurrency test-v45-job-control test-v45-target test-v45-native-runtime embed go-binding csharp-binding node-binding python-binding bindings test-build-boundary test-embed test-go-binding test-csharp-binding test-node-binding test-python-binding test-bindings test-all test benchmark-memory-10k benchmark-10k test-tracking-scaling test-full-build-scaling test-recovery-epoch test-performance-scaling test-sanitize test-sanitize-lifetime memory-safety-smoke all clean test-jsonic test-jsonic-sync test-markuppp-sync test-json test-json-schema test-runtime-value test-cp15-numeric-repair test-cp17-bytes test-cp18-bytes test-cp19-bytes test-cp21-bytes test-cp18-bytes-sanitize test-cp18-bytes-tsan test-console test-progress-render test-progress-pty test-snap-contract test-distribution-summary test-version-consistency test-diagnostics test-minify test-json-schema-integration test-markup-json-directives test-engine test-engine-bindings test-engine-loaders test-engine-source-read test-engine-pathto test-engine-concurrency test-engine-project test-engine-reload test-engine-pagination-snapshot test-c-abi test-c-abi-c-smoke test-host-seam benchmark-c-abi test-project-state test-project-host test-public-header test-conformance test-content test-commands test-comments test-ownership-concurrency test-zero-mutation test-repair-campaign test-pagination-ordering test-json-binding test-control-flow test-requirements test-path-alias test-path-safety test-metadata-safety test-template-optional test-contracts test-init-targets test-init-lock test-unreadable-source test-incremental-modified-immediate test-runtime2-modified-race test-runtime2-dynamic-reads test-runtime2-fs-freedom test-runtime2-scanner-parity test-v41-certification test-v42-language test-v42-struct test-v43-language test-parser-hardening test-prepared-method-parity test-v48-warn test-v48-bytes-index-parity test-v48-comparison-parity test-v48-callable-parity test-v48-map-set-index test-v48-map-set-scaling test-v48-prepared-object-literal-parity test-v48-prepared-object-literal-guard test-v48-prepared-collection-parity test-v48-prepared-collection-guard test-init-migration test-macos-runner-policy install uninstall clean distclean pristine


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


test-scripting-perf: $(TARGET)
	NIFT_BIN="$(CURDIR)/$(TARGET)" bash tests/scripting_perf_guard.sh

test-performance-scaling: test-tracking-scaling test-full-build-scaling test-recovery-epoch test-scripting-perf


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

# --- Lifetime sanitizer (ASan/UBSan with stack use-after-scope retained) -------
LIFETIME_RUN = env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=$$(test "$$(uname -s)" = Darwin && echo 0 || echo 1):halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1

$(TEST_DIR)/san-lifetime/%.o: %.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(SAN_CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SAN_LIFETIME_FLAGS) -MMD -MP -c "$<" -o "$@"

$(TEST_DIR)/san-lifetime/%.o: %.c
	mkdir -p "$(dir $@)"
	$(CC) -Imarkuppp/vendor/cmark -std=c99 -Wall -Wextra -pedantic $(SAN_LIFETIME_FLAGS) -MMD -MP -c "$<" -o "$@"

$(SAN_LIFETIME_TARGET): $(SAN_LIFETIME_OBJECTS) $(SAN_LIBFFI_A)
	mkdir -p "$(TEST_DIR)"
	$(CXX) -std=c++17 -pthread $(SAN_LIFETIME_FLAGS) $(SAN_LIFETIME_OBJECTS) $(SAN_LIBFFI_A) -o "$@"

# Prove the two sanitizer profiles genuinely differ (lifetime traps
# stack-use-after-scope; deep does not), then run a representative SHALLOW
# corpus under the lifetime sanitizer. Deep-recursion tests belong to the
# deep-capable profile only (the instrumentation inflates the evaluator frame).
test-sanitize-lifetime: $(SAN_LIFETIME_TARGET)
	@set -e; \
	$(CXX) $(SAN_LIFETIME_FLAGS) -pthread tests/sanitizer_use_after_scope_canary.cpp -o "$(TEST_DIR)/canary-lifetime"; \
	if err=$$(env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$(TEST_DIR)/canary-lifetime" 2>&1); then \
	  echo "FAIL: lifetime sanitizer did not trap stack-use-after-scope" >&2; exit 1; fi; \
	printf '%s' "$$err" | grep -q 'stack-use-after-scope' || { echo "FAIL: lifetime sanitizer lacks use-after-scope instrumentation:" >&2; printf '%s\n' "$$err" >&2; exit 1; }; \
	$(CXX) $(SANITIZER_FLAGS) -pthread tests/sanitizer_use_after_scope_canary.cpp -o "$(TEST_DIR)/canary-deep"; \
	err=$$(env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$(TEST_DIR)/canary-deep" 2>&1) || true; \
	printf '%s' "$$err" | grep -q 'stack-use-after-scope' && { echo "FAIL: deep sanitizer unexpectedly has use-after-scope instrumentation" >&2; exit 1; } || true; \
	echo "sanitizer profiles differ: lifetime use-after-scope ON, deep OFF"
	$(LIFETIME_RUN) "$(SAN_LIFETIME_TARGET)" --version
	$(LIFETIME_RUN) NIFT_BIN="$(CURDIR)/$(SAN_LIFETIME_TARGET)" tests/v43_cp0_cp14_smoke.sh
	$(LIFETIME_RUN) NIFT_BIN="$(CURDIR)/$(SAN_LIFETIME_TARGET)" tests/v43_cp15_cp34_smoke.sh
	$(LIFETIME_RUN) NIFT_BIN="$(CURDIR)/$(SAN_LIFETIME_TARGET)" tests/collection_ops_smoke.sh
	$(LIFETIME_RUN) NIFT_BIN="$(CURDIR)/$(SAN_LIFETIME_TARGET)" tests/v43_cp117_object_methods_smoke.sh
	$(LIFETIME_RUN) NIFT_BIN="$(CURDIR)/$(SAN_LIFETIME_TARGET)" tests/v43_cp51_cp70_scripting_smoke.sh
	$(LIFETIME_RUN) NIFT_BIN="$(CURDIR)/$(SAN_LIFETIME_TARGET)" tests/v46_b4_cp3_recoverable_errors.sh
	$(LIFETIME_RUN) NIFT_BIN="$(CURDIR)/$(SAN_LIFETIME_TARGET)" tests/v45_threads.sh
	$(LIFETIME_RUN) NIFT_BIN="$(CURDIR)/$(SAN_LIFETIME_TARGET)" tests/v44_ast_expression_smoke.sh
	$(LIFETIME_RUN) NIFT_BIN="$(CURDIR)/$(SAN_LIFETIME_TARGET)" bash tests/v44_ast_differential_corpus.sh
	echo "lifetime sanitizer shallow corpus passed"

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
	$(LOAD_SENSITIVE_DEFER) NIFT="$(CURDIR)/$(TARGET)" tests/v44_perf_scaling_smoke.sh
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

CP19_EXPR_UNIT := $(TEST_DIR)/cp19/expression-unit$(EXEEXT)
CP19_EXPR_OBJECTS := $(RT_VALUE_SOURCES:.cpp=.o) src/RuntimeValue.o
CP19_EXPR_SAN_OBJECTS := $(patsubst %.cpp,$(TEST_DIR)/san/%.o,$(RT_VALUE_SOURCES) src/RuntimeValue.cpp)
$(CP19_EXPR_UNIT): tests/rt/vm_expression.cpp $(CP19_EXPR_OBJECTS) $(RT_VALUE_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/expression-sanitize$(EXEEXT): tests/rt/vm_expression.cpp $(CP19_EXPR_SAN_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) $< $(CP19_EXPR_SAN_OBJECTS) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@
.PHONY: test-cp19-expression-sanitize
test-cp19-expression-sanitize: $(TEST_DIR)/cp19/expression-sanitize$(EXEEXT)
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 $<

# CP19 shared-entry slice: real eval result/parity and affirmative routing on
# both dispatches, plus a compiled mutant restoring the legacy-only bypass.
CP19_EVAL_MUTANT := $(TEST_DIR)/cp19/nift-eval-bypass$(EXEEXT)
$(TEST_DIR)/cp19/CLI-bypass.o: src/CLI.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_CP19_EVAL_MUTANT_BYPASS=1 -MMD -MP -c $< -o $@
-include $(TEST_DIR)/cp19/CLI-bypass.d
$(CP19_EVAL_MUTANT): $(filter-out src/CLI.o,$(CLI_OBJECTS)) $(TEST_DIR)/cp19/CLI-bypass.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/CLI.o,$(CLI_OBJECTS)) $(TEST_DIR)/cp19/CLI-bypass.o $(LDLIBS) -o $@
.PHONY: test-cp19-shared-eval
test: test-cp19-shared-eval
test-rt-vm-sanitize: test-cp19-expression-sanitize
test-cp19-shared-eval: $(TARGET) $(CP13B_SWITCH_NIFT) $(CP19_EVAL_MUTANT) $(CP19_EXPR_UNIT)
	$(CP19_EXPR_UNIT)
	$(PYTHON) tests/runtime2/cp19/eval_entry.py --nift "$(CURDIR)/$(TARGET)" --report "$(TEST_DIR)/cp19/threaded.jsonl"
	$(PYTHON) tests/runtime2/cp19/eval_entry.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --report "$(TEST_DIR)/cp19/switch.jsonl"
	$(PYTHON) tests/runtime2/cp19/eval_entry.py --nift "$(CURDIR)/$(CP19_EVAL_MUTANT)" --expect-bypass --report "$(TEST_DIR)/cp19/bypass.jsonl"

# CP19 R1: frozen reviewer regressions and real synthetic-prefix negative control.
CP19_RETURN_MUTANT := $(TEST_DIR)/cp19/nift-return-prefix$(EXEEXT)
$(TEST_DIR)/cp19/compiler-return.o: src/rt/compiler.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_CP19_EVAL_MUTANT_RETURN=1 -MMD -MP -c $< -o $@
-include $(TEST_DIR)/cp19/compiler-return.d
$(CP19_RETURN_MUTANT): $(filter-out src/rt/compiler.o,$(CLI_OBJECTS)) $(TEST_DIR)/cp19/compiler-return.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/rt/compiler.o,$(CLI_OBJECTS)) $(TEST_DIR)/cp19/compiler-return.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/ownership-review$(EXEEXT): tests/runtime2/cp19/ownership_review.cpp $(CP19_EXPR_OBJECTS) tests/runtime2/cp19/ownership_env.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests/rt -include tests/runtime2/cp19/ownership_env.h $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/ownership-review-sanitize$(EXEEXT): tests/runtime2/cp19/ownership_review.cpp $(CP19_EXPR_SAN_OBJECTS) tests/runtime2/cp19/ownership_env.h
	@mkdir -p $(dir $@)
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt -include tests/runtime2/cp19/ownership_env.h -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) $< $(CP19_EXPR_SAN_OBJECTS) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@
$(TEST_DIR)/cp19/ownership-review-switch$(EXEEXT): tests/runtime2/cp19/ownership_review.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o tests/runtime2/cp19/ownership_env.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests/rt -include tests/runtime2/cp19/ownership_env.h $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/vm-switch-sanitize.o: src/rt/vm.cpp $(RT_VALUE_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(SAN_CPPFLAGS) -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) -DNIFT_RT_VM_SWITCH -c $< -o $@
$(TEST_DIR)/cp19/ownership-review-switch-sanitize$(EXEEXT): tests/runtime2/cp19/ownership_review.cpp $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o tests/runtime2/cp19/ownership_env.h
	@mkdir -p $(dir $@)
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt -include tests/runtime2/cp19/ownership_env.h -std=c++17 -Wall -Wextra -pedantic -pthread $(SANITIZER_FLAGS) $< $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@
.PHONY: test-cp19-r1 test-cp19-r1-sanitize test-cp19-expression-development
test-cp19-expression-development: $(TARGET) $(CP13B_SWITCH_NIFT)
	$(PYTHON) tests/runtime2/cp19/expressions_complete.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/expressions-threaded.jsonl"
	$(PYTHON) tests/runtime2/cp19/expressions_complete.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/expressions-switch.jsonl"
test-cp19-r1: test-cp19-shared-eval $(CP19_RETURN_MUTANT) $(TEST_DIR)/cp19/ownership-review$(EXEEXT) $(TEST_DIR)/cp19/ownership-review-switch$(EXEEXT)
	$(TEST_DIR)/cp19/ownership-review$(EXEEXT)
	$(TEST_DIR)/cp19/ownership-review-switch$(EXEEXT)
	$(PYTHON) tests/runtime2/cp19/eval_r1.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/r1-threaded.jsonl"
	$(PYTHON) tests/runtime2/cp19/eval_r1.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/r1-switch.jsonl"
	$(PYTHON) tests/runtime2/cp19/eval_r1.py --nift "$(CURDIR)/$(CP19_RETURN_MUTANT)" --out "$(TEST_DIR)/cp19/return-mutant.jsonl" --expect-return-prefix
	$(PYTHON) tests/runtime2/cp19/reviewer_matrix.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/review-threaded.json"
	$(PYTHON) tests/runtime2/cp19/check_review_matrix.py "$(TEST_DIR)/cp19/review-threaded.json"
	$(PYTHON) tests/runtime2/cp19/reviewer_matrix.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/review-switch.json"
	$(PYTHON) tests/runtime2/cp19/check_review_matrix.py "$(TEST_DIR)/cp19/review-switch.json"
test-cp19-r1-sanitize: test-cp19-expression-sanitize $(TEST_DIR)/cp19/ownership-review-sanitize$(EXEEXT) $(TEST_DIR)/cp19/ownership-review-switch-sanitize$(EXEEXT)
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 $(TEST_DIR)/cp19/ownership-review-sanitize$(EXEEXT)
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 $(TEST_DIR)/cp19/ownership-review-switch-sanitize$(EXEEXT)
test: test-cp19-r1
test-rt-vm-sanitize: test-cp19-r1-sanitize

# CP19 Slice 1 (D-042): pure classifier unit test and the durable shell
# dispatch regression suite. Three test-only mutant binaries are built below:
# M1 restores call-then-fallback, M2 discards quote/escape provenance, and M3
# restores eager interpolation. Normal builds contain none of their code.
SHELL_DISPATCH_UNIT := $(TEST_DIR)/shell-dispatch-unit$(EXEEXT)
SHELL_DISPATCH_MUTANT := $(TEST_DIR)/nift-shell-dispatch-mutant$(EXEEXT)
SHELL_DISPATCH_MUTANT_QUOTING := $(TEST_DIR)/nift-shell-dispatch-mutant-quoting$(EXEEXT)
SHELL_DISPATCH_MUTANT_EAGER := $(TEST_DIR)/nift-shell-dispatch-mutant-eager$(EXEEXT)
SHELL_DISPATCH_MUTANT_SYNTAX := $(TEST_DIR)/nift-shell-dispatch-mutant-syntax$(EXEEXT)
SHELL_DISPATCH_MUTANT_DIR := $(TEST_DIR)/shell-dispatch-mutants
SHELL_DISPATCH_LINK_OBJECTS := $(filter-out src/CLI.o,$(CLI_OBJECTS))

$(SHELL_DISPATCH_UNIT): tests/shell_dispatch_unit.cpp src/shell/CommandDispatch.cpp src/shell/CommandDispatch.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/shell_dispatch_unit.cpp src/shell/CommandDispatch.cpp -o $@

$(SHELL_DISPATCH_MUTANT_DIR)/fallback/CLI.o: src/CLI.cpp src/shell/CommandDispatch.h src/shell/CommandDispatch.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SHELL_DISPATCH_MUTANT=1 -MMD -MP -c $< -o $@

$(SHELL_DISPATCH_MUTANT_DIR)/quoting/CLI.o: src/CLI.cpp src/shell/CommandDispatch.h src/shell/CommandDispatch.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SHELL_DISPATCH_MUTANT_QUOTING=1 -MMD -MP -c $< -o $@

$(SHELL_DISPATCH_MUTANT_DIR)/eager/CLI.o: src/CLI.cpp src/shell/CommandDispatch.h src/shell/CommandDispatch.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SHELL_DISPATCH_MUTANT_EAGER_INTERPOLATION=1 -MMD -MP -c $< -o $@

$(SHELL_DISPATCH_MUTANT_DIR)/syntax/CLI.o: src/CLI.cpp src/shell/CommandDispatch.h src/shell/CommandDispatch.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SHELL_DISPATCH_MUTANT_SYNTAX=1 -MMD -MP -c $< -o $@

$(SHELL_DISPATCH_MUTANT): $(SHELL_DISPATCH_LINK_OBJECTS) $(SHELL_DISPATCH_MUTANT_DIR)/fallback/CLI.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SHELL_DISPATCH_LINK_OBJECTS) $(SHELL_DISPATCH_MUTANT_DIR)/fallback/CLI.o $(LDLIBS) -o $@

$(SHELL_DISPATCH_MUTANT_QUOTING): $(SHELL_DISPATCH_LINK_OBJECTS) $(SHELL_DISPATCH_MUTANT_DIR)/quoting/CLI.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SHELL_DISPATCH_LINK_OBJECTS) $(SHELL_DISPATCH_MUTANT_DIR)/quoting/CLI.o $(LDLIBS) -o $@

$(SHELL_DISPATCH_MUTANT_EAGER): $(SHELL_DISPATCH_LINK_OBJECTS) $(SHELL_DISPATCH_MUTANT_DIR)/eager/CLI.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SHELL_DISPATCH_LINK_OBJECTS) $(SHELL_DISPATCH_MUTANT_DIR)/eager/CLI.o $(LDLIBS) -o $@

$(SHELL_DISPATCH_MUTANT_SYNTAX): $(SHELL_DISPATCH_LINK_OBJECTS) $(SHELL_DISPATCH_MUTANT_DIR)/syntax/CLI.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SHELL_DISPATCH_LINK_OBJECTS) $(SHELL_DISPATCH_MUTANT_DIR)/syntax/CLI.o $(LDLIBS) -o $@

test-cp19-s1-dispatch-unit: $(SHELL_DISPATCH_UNIT)
	$(SHELL_DISPATCH_UNIT)

test-cp19-s1-dispatch: $(TARGET) $(SHELL_DISPATCH_UNIT) $(SHELL_DISPATCH_MUTANT) $(SHELL_DISPATCH_MUTANT_QUOTING) $(SHELL_DISPATCH_MUTANT_EAGER) $(SHELL_DISPATCH_MUTANT_SYNTAX)
	$(SHELL_DISPATCH_UNIT)
	@if [ -n "$(PYTHON)" ]; then NIFT="$(CURDIR)/$(TARGET)" $(PYTHON) -u tests/shell_dispatch.py; st=$$?; if [ $$st -eq 77 ]; then echo "  (skipped: shell dispatch needs POSIX)"; else exit $$st; fi; else echo "  (skipped: python3 unavailable)"; fi
	@if [ -n "$(PYTHON)" ]; then NIFT="$(CURDIR)/$(TARGET)" $(PYTHON) -u tests/cp19_completion.py; st=$$?; if [ $$st -eq 77 ]; then echo "  (skipped: completion PTY needs POSIX)"; else exit $$st; fi; else echo "  (skipped: python3 unavailable)"; fi
	@if [ -n "$(PYTHON)" ]; then NIFT="$(CURDIR)/$(SHELL_DISPATCH_MUTANT)" $(PYTHON) -u tests/shell_dispatch.py --mutant-kind m1; st=$$?; if [ $$st -eq 77 ]; then echo "  (skipped: shell dispatch needs POSIX)"; else exit $$st; fi; else echo "  (skipped: python3 unavailable)"; fi
	@if [ -n "$(PYTHON)" ]; then NIFT="$(CURDIR)/$(SHELL_DISPATCH_MUTANT_QUOTING)" $(PYTHON) -u tests/shell_dispatch.py --mutant-kind m2; st=$$?; if [ $$st -eq 77 ]; then echo "  (skipped: shell dispatch needs POSIX)"; else exit $$st; fi; else echo "  (skipped: python3 unavailable)"; fi
	@if [ -n "$(PYTHON)" ]; then NIFT="$(CURDIR)/$(SHELL_DISPATCH_MUTANT_EAGER)" $(PYTHON) -u tests/shell_dispatch.py --mutant-kind m3; st=$$?; if [ $$st -eq 77 ]; then echo "  (skipped: shell dispatch needs POSIX)"; else exit $$st; fi; else echo "  (skipped: python3 unavailable)"; fi
	@if [ -n "$(PYTHON)" ]; then NIFT="$(CURDIR)/$(SHELL_DISPATCH_MUTANT_SYNTAX)" $(PYTHON) -u tests/shell_dispatch.py --mutant-kind m4; st=$$?; if [ $$st -eq 77 ]; then echo "  (skipped: shell dispatch needs POSIX)"; else exit $$st; fi; else echo "  (skipped: python3 unavailable)"; fi

test: test-cp19-s1-dispatch

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

test-v46-b5-cp13: test-v46-b5-cp12
	bash tests/v46_b5_cp13_determinism.sh

test-v46-b5-cp14: test-v46-b5-cp13
	bash tests/v46_b5_cp14_final_certification.sh

.PHONY: test-v46-b4-cp2 test-v46-b4-cp3 test-v46-b4-pre-cp4 test-v46-b4-cp4a test-v46-b4-cp4b test-v46-b4-cp4b-stream-operators test-v46-b4-cp4c test-v46-b4-cp5a test-v46-b4-cp6 test-v46-b4-cp5b test-v46-b4-cp7 test-v46-b4-cp8 test-v46-b4-cp9 test-v46-b5-cp10 test-v46-b5-cp11 test-v46-b5-cp12 test-v46-b5-cp13 test-v46-b5-cp14

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

# CP-F1: source coordinate composition is independent of evaluator behavior.
$(TEST_DIR)/source-view-unit$(EXEEXT): tests/source_view_unit.cpp src/SourceView.h src/Diagnostic.h
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/source_view_unit.cpp -o $@

.PHONY: test-source-view
test-source-view: $(TEST_DIR)/source-view-unit$(EXEEXT)
	$(TEST_DIR)/source-view-unit$(EXEEXT)

test: test-source-view

$(TEST_DIR)/source-translation-unit$(EXEEXT): tests/source_translation_unit.cpp $(filter-out src/nift.o src/CLI.o,$(CLI_OBJECTS))
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/source_translation_unit.cpp $(filter-out src/nift.o src/CLI.o,$(CLI_OBJECTS)) $(LDLIBS) -o $@

.PHONY: test-source-translation
test-source-translation: $(TEST_DIR)/source-translation-unit$(EXEEXT)
	$(TEST_DIR)/source-translation-unit$(EXEEXT)

test: test-source-translation

# Runtime 2.0 CP10: script lexer/parser (src/rt). Not linked into nift yet;
# compiled only into these test binaries.
# One shared list (also read by the Python and Node binding builds).
RT_PARSER_SOURCES := $(filter-out \#%,$(shell grep -v '^\#' src/rt/parser-sources.txt))
RT_PARSER_HEADERS := src/rt/lexer.h src/rt/ast.h src/rt/source_scan.h src/rt/parser.h src/rt/parser_internal.h \
	src/rt/statement_parser.h src/rt/declarations.h src/rt/control_flow.h src/rt/expr_parser.h src/rt/expr_postfix.h \
	src/rt/expr_literals.h src/rt/prevalidate.h tests/rt/rt_parser_tool.h

$(TEST_DIR)/rt-parse-dump$(EXEEXT): tests/rt/parse_dump.cpp $(RT_PARSER_SOURCES) $(RT_PARSER_HEADERS)
	mkdir -p $(TEST_DIR)
	$(CXX) -Isrc -Itests/rt $(CXXFLAGS) tests/rt/parse_dump.cpp $(RT_PARSER_SOURCES) -o $@

$(TEST_DIR)/rt-parser-unit$(EXEEXT): tests/rt/parser_unit.cpp $(RT_PARSER_SOURCES) $(RT_PARSER_HEADERS)
	mkdir -p $(TEST_DIR)
	$(CXX) -Isrc -Itests/rt $(CXXFLAGS) tests/rt/parser_unit.cpp $(RT_PARSER_SOURCES) -o $@

# The legacy objects only (CP11 links src/rt into nift; the oracle compiles the
# parser sources itself, so the src/rt objects are left out to avoid duplicates).
RT_LEGACY_ORACLE_OBJECTS := $(filter-out src/nift.o src/CLI.o $(RT_LINK_SOURCES:.cpp=.o),$(CLI_OBJECTS))
# CP16: the legacy objects use the Host layer (kept above), whose handle table
# needs these src/rt objects.
RT_LEGACY_ORACLE_OBJECTS += src/rt/handle.o src/rt/value.o src/rt/string.o src/rt/heap_account.o
$(TEST_DIR)/rt-legacy-oracle$(EXEEXT): tests/rt/legacy_oracle.cpp $(RT_PARSER_SOURCES) $(RT_PARSER_HEADERS) $(RT_LEGACY_ORACLE_OBJECTS)
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/legacy_oracle.cpp $(RT_PARSER_SOURCES) $(RT_LEGACY_ORACLE_OBJECTS) $(LDLIBS) -o $@

.PHONY: test-rt-parser
test-rt-parser: $(TEST_DIR)/rt-parser-unit$(EXEEXT) $(TEST_DIR)/rt-parse-dump$(EXEEXT) $(TEST_DIR)/rt-legacy-oracle$(EXEEXT) $(TARGET)
	$(TEST_DIR)/rt-parser-unit$(EXEEXT)
	python3 tests/runtime2/rt_parser_corpus.py --build $(TEST_DIR) --nift ./$(TARGET)

test: test-rt-parser
test: test-rt-vm test-rt-vm-mutants

# ASan/UBSan run of the rt parser over the script corpora plus fuzzing (CP10
# evidence; slower, not part of `make test`).
.PHONY: test-rt-parser-asan
test-rt-parser-asan:
	mkdir -p $(TEST_DIR)/rt-asan
	$(CXX) -Isrc -Itests/rt -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		tests/rt/parser_unit.cpp $(RT_PARSER_SOURCES) -o $(TEST_DIR)/rt-asan/rt-parser-unit
	$(CXX) -Isrc -Itests/rt -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		tests/rt/parse_dump.cpp $(RT_PARSER_SOURCES) -o $(TEST_DIR)/rt-asan/rt-parse-dump
	$(TEST_DIR)/rt-asan/rt-parser-unit
	python3 tests/runtime2/rt_parser_corpus.py --sanitized $(TEST_DIR)/rt-asan/rt-parse-dump

$(TEST_DIR)/ast-source-unit$(EXEEXT): tests/ast_source_unit.cpp src/Ast.cpp src/Ast.h src/RuntimeValue.cpp src/RuntimeValue.h
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/ast_source_unit.cpp src/Ast.cpp src/RuntimeValue.cpp -o $@

.PHONY: test-ast-source
test-ast-source: $(TEST_DIR)/ast-source-unit$(EXEEXT)
	$(TEST_DIR)/ast-source-unit$(EXEEXT)

test: test-ast-source

$(TEST_DIR)/source-map-probe$(EXEEXT): tests/source_map_probe.cpp $(filter-out src/nift.o src/CLI.o,$(CLI_OBJECTS))
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/source_map_probe.cpp $(filter-out src/nift.o src/CLI.o,$(CLI_OBJECTS)) $(LDLIBS) -o $@

.PHONY: test-source-map
test-source-map: $(TEST_DIR)/source-map-probe$(EXEEXT)
	python3 tests/source_map_matrix.py $(TEST_DIR)/source-map-probe$(EXEEXT)

test: test-source-map

.PHONY: test-binding-warnings

.PHONY: test-v49-selector-parity
test-v49-selector-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" python3 tests/v49_selector_parity.py

.PHONY: test-v49-glob-order
test-v49-glob-order: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" python3 tests/v49_glob_order.py

# Dedicated test-only path conversion instrumentation.
$(TEST_DIR)/glob-keys/ParserHostHelpers.o: src/ParserHostHelpers.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_GLOB_KEY_STATS -MMD -MP -c $< -o $@
-include $(TEST_DIR)/glob-keys/ParserHostHelpers.d
GLOB_KEY_GUARD := $(TEST_DIR)/nift-glob-key-guard$(EXEEXT)
$(GLOB_KEY_GUARD): $(filter-out src/ParserHostHelpers.o,$(CLI_OBJECTS)) $(TEST_DIR)/glob-keys/ParserHostHelpers.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/ParserHostHelpers.o,$(CLI_OBJECTS)) $(TEST_DIR)/glob-keys/ParserHostHelpers.o $(LDLIBS) -o $@
.PHONY: test-v49-glob-key-guard
test-v49-glob-key-guard: $(GLOB_KEY_GUARD)
	NIFT="$(CURDIR)/$(GLOB_KEY_GUARD)" python3 tests/v49_glob_key_guard.py

JSON_PREFLIGHT_GUARD := $(TEST_DIR)/nift-json-preflight-guard$(EXEEXT)
$(JSON_PREFLIGHT_GUARD): tests/v49_json_preflight_guard.cpp src/RuntimeValue.cpp src/RuntimeValue.h src/RuntimeJson.h
	mkdir -p "$(TEST_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_JSON_PREFLIGHT_STATS tests/v49_json_preflight_guard.cpp src/RuntimeValue.cpp -o "$@"
.PHONY: test-v49-json-preflight-guard
test-v49-json-preflight-guard: $(JSON_PREFLIGHT_GUARD)
	NIFT_JSON_GUARD="$(CURDIR)/$(JSON_PREFLIGHT_GUARD)" python3 tests/v49_json_preflight_guard.py

.PHONY: test-v49-group-scaling
test-v49-group-scaling: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" python3 tests/v49_group_scaling.py

$(TEST_DIR)/v49-call-outcome-unit$(EXEEXT): tests/v49_call_outcome_unit.cpp src/Ast.cpp src/Ast.h src/RuntimeValue.cpp src/RuntimeValue.h
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/v49_call_outcome_unit.cpp src/Ast.cpp src/RuntimeValue.cpp -o $@

.PHONY: test-v49-call-outcome
test-v49-call-outcome: $(TEST_DIR)/v49-call-outcome-unit$(EXEEXT)
	$(TEST_DIR)/v49-call-outcome-unit$(EXEEXT)

test: test-v49-call-outcome

# Explicit Valgrind allocation guard; no wall-clock threshold.
.PHONY: test-v410-allocation-guard
test-v410-allocation-guard: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" python3 tests/v410_allocation_guard.py

.PHONY: test-v410-sort-factory-parity test-v410-object-member-parity test-v410-object-member-unit
test-v410-sort-factory-parity: $(TARGET)
	python3 tests/v410_sort_factory_parity.py --nift ./$(TARGET)

test-v410-object-member-parity: $(TARGET)
	python3 tests/v410_object_member_parity.py --nift ./$(TARGET)

test-v410-object-member-unit:
	mkdir -p .build
	$(CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -Isrc -Ijsonic/include tests/v410_object_member_unit.cpp src/RuntimeValue.cpp -o .build/v410-object-member-unit
	.build/v410-object-member-unit

$(TEST_DIR)/object-lookup/RuntimeValue.o: src/RuntimeValue.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_OBJECT_LOOKUP_STATS -MMD -MP -c $< -o $@

-include $(TEST_DIR)/object-lookup/RuntimeValue.d
OBJECT_LOOKUP_GUARD := $(TEST_DIR)/nift-object-lookup-guard$(EXEEXT)
$(OBJECT_LOOKUP_GUARD): $(filter-out src/RuntimeValue.o,$(CLI_OBJECTS)) $(TEST_DIR)/object-lookup/RuntimeValue.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/RuntimeValue.o,$(CLI_OBJECTS)) $(TEST_DIR)/object-lookup/RuntimeValue.o $(LDLIBS) -o $@

.PHONY: test-v410-object-member-guard
test-v410-object-member-guard: $(OBJECT_LOOKUP_GUARD)
	python3 tests/v410_object_member_guard.py --nift ./$(OBJECT_LOOKUP_GUARD)

test: test-v410-sort-factory-parity test-v410-object-member-parity test-v410-object-member-unit test-v410-object-member-guard

.PHONY: test-v410-oracle-paths
test-v410-oracle-paths:
	python3 tests/v410_oracle_paths_test.py

test-v410-sort-factory-parity test-v410-object-member-parity: test-v410-oracle-paths

# Test-only counters for shared glob-base canonicalization.
$(TEST_DIR)/glob-relative/ParserExpression.o: src/ParserExpression.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_GLOB_PATH_STATS -MMD -MP -c $< -o $@
-include $(TEST_DIR)/glob-relative/ParserExpression.d
GLOB_RELATIVE_GUARD := $(TEST_DIR)/nift-glob-relative-guard$(EXEEXT)
$(GLOB_RELATIVE_GUARD): $(filter-out src/ParserExpression.o,$(CLI_OBJECTS)) $(TEST_DIR)/glob-relative/ParserExpression.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/ParserExpression.o,$(CLI_OBJECTS)) $(TEST_DIR)/glob-relative/ParserExpression.o $(LDLIBS) -o $@
.PHONY: test-v410-glob-relative-guard test-v410-glob-parity
test-v410-glob-relative-guard: $(GLOB_RELATIVE_GUARD)
	NIFT="$(CURDIR)/$(GLOB_RELATIVE_GUARD)" python3 tests/v410_glob_relative_guard.py
test-v410-glob-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" python3 tests/v410_glob_parity.py
test: test-v410-glob-relative-guard test-v410-glob-parity

.PHONY: test-v410-native-dispatch-parity
test-v410-native-dispatch-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" python3 tests/v410_native_dispatch_parity.py
test: test-v410-native-dispatch-parity

.PHONY: test-v410-string-replace-parity
test-v410-string-replace-parity: $(TARGET)
	NIFT="$(CURDIR)/$(TARGET)" python3 tests/v410_string_replace_parity.py
test: test-v410-string-replace-parity

.PHONY: test-v410-callback-overlay-parity
test-v410-callback-overlay-parity: $(TARGET)
	python3 tests/v410_callback_overlay_parity.py --nift "$(CURDIR)/$(TARGET)"

test: test-v410-callback-overlay-parity

.PHONY: test-v410-callback-overlay-guard
test-v410-callback-overlay-guard: $(LAMBDA_CACHE_GUARD)
	NIFT="$(CURDIR)/$(LAMBDA_CACHE_GUARD)" python3 tests/v410_callback_overlay_guard.py

test: test-v410-callback-overlay-guard

# Operation-local glob prefix and terminal materialization scaling.
$(TEST_DIR)/glob-prefix/ParserHostHelpers.o: src/ParserHostHelpers.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_GLOB_PREFIX_STATS -MMD -MP -c $< -o $@
$(TEST_DIR)/glob-prefix/ParserExpression.o: src/ParserExpression.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_GLOB_PREFIX_STATS -MMD -MP -c $< -o $@
-include $(TEST_DIR)/glob-prefix/ParserHostHelpers.d $(TEST_DIR)/glob-prefix/ParserExpression.d
GLOB_PREFIX_GUARD := $(TEST_DIR)/nift-glob-prefix-guard$(EXEEXT)
$(GLOB_PREFIX_GUARD): $(filter-out src/ParserHostHelpers.o src/ParserExpression.o,$(CLI_OBJECTS)) $(TEST_DIR)/glob-prefix/ParserHostHelpers.o $(TEST_DIR)/glob-prefix/ParserExpression.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/ParserHostHelpers.o src/ParserExpression.o,$(CLI_OBJECTS)) $(TEST_DIR)/glob-prefix/ParserHostHelpers.o $(TEST_DIR)/glob-prefix/ParserExpression.o $(LDLIBS) -o $@
.PHONY: test-v410-glob-prefix-guard test-v410-glob-prefix-parity
test-v410-glob-prefix-guard: $(GLOB_PREFIX_GUARD)
	NIFT_PREFIX_STATS=$(abspath $(GLOB_PREFIX_GUARD)) $(PYTHON) tests/v410_glob_prefix_guard.py
test-v410-glob-prefix-parity: $(TARGET)
	NIFT=$(abspath $(TARGET)) $(PYTHON) tests/v410_glob_prefix_parity.py
test: test-v410-glob-prefix-guard test-v410-glob-prefix-parity

# Canonical filesystem recipe reuse; test-only counters.
$(TEST_DIR)/fs-recipe/ParserExpression.o: src/ParserExpression.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_FS_RECIPE_STATS -MMD -MP -c $< -o $@
$(TEST_DIR)/fs-recipe/ParserTemplate.o: src/ParserTemplate.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_TEST_FS_RECIPE_STATS -MMD -MP -c $< -o $@
-include $(TEST_DIR)/fs-recipe/ParserExpression.d $(TEST_DIR)/fs-recipe/ParserTemplate.d
FS_RECIPE_GUARD := $(TEST_DIR)/nift-fs-recipe-guard$(EXEEXT)
$(FS_RECIPE_GUARD): $(filter-out src/ParserExpression.o src/ParserTemplate.o,$(CLI_OBJECTS)) $(TEST_DIR)/fs-recipe/ParserExpression.o $(TEST_DIR)/fs-recipe/ParserTemplate.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/ParserExpression.o src/ParserTemplate.o,$(CLI_OBJECTS)) $(TEST_DIR)/fs-recipe/ParserExpression.o $(TEST_DIR)/fs-recipe/ParserTemplate.o $(LDLIBS) -o $@
.PHONY: test-v410-filesystem-recipe-parity test-v410-filesystem-recipe-guard
test-v410-filesystem-recipe-parity: $(TARGET)
	NIFT=$(abspath $(TARGET)) $(PYTHON) tests/v410_filesystem_recipe_parity.py
test-v410-filesystem-recipe-guard: $(FS_RECIPE_GUARD)
	NIFT_FS_STATS=$(abspath $(FS_RECIPE_GUARD)) $(PYTHON) tests/v410_filesystem_recipe_guard.py
test: test-v410-filesystem-recipe-parity test-v410-filesystem-recipe-guard

LARGE_STRING_GUARD := $(TEST_DIR)/large-string-guard$(EXEEXT)
$(LARGE_STRING_GUARD): tests/v410_large_string_guard.cpp $(filter-out src/nift.o src/CLI.o,$(CLI_OBJECTS)) $(LIBFFI_A)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/v410_large_string_guard.cpp $(filter-out src/nift.o src/CLI.o,$(CLI_OBJECTS)) $(LDFLAGS) $(LDLIBS) -o $@
.PHONY: test-v410-large-string-parity test-v410-large-string-guard
test-v410-large-string-parity: $(TARGET)
	NIFT=$(abspath $(TARGET)) $(PYTHON) tests/v410_large_string_parity.py
test-v410-large-string-guard: $(LARGE_STRING_GUARD)
	$(LARGE_STRING_GUARD)
test: test-v410-large-string-parity test-v410-large-string-guard

FILE_BUFFER_GUARD := $(TEST_DIR)/file-buffer-guard$(EXEEXT)
$(FILE_BUFFER_GUARD): tests/v410_file_buffer_guard.cpp $(filter-out src/nift.o src/CLI.o,$(CLI_OBJECTS)) $(LIBFFI_A)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/v410_file_buffer_guard.cpp $(filter-out src/nift.o src/CLI.o,$(CLI_OBJECTS)) $(LDFLAGS) $(LDLIBS) -o $@
.PHONY: test-v410-file-buffer-parity test-v410-file-buffer-guard
test-v410-file-buffer-parity: $(TARGET)
	NIFT=$(abspath $(TARGET)) $(PYTHON) tests/v410_file_buffer_parity.py
test-v410-file-buffer-guard: $(FILE_BUFFER_GUARD)
	$(FILE_BUFFER_GUARD)
test: test-v410-file-buffer-parity test-v410-file-buffer-guard

FILE_BUFFER_LIFETIME_GUARD := $(TEST_DIR)/file-buffer-lifetime-guard$(EXEEXT)
FILE_BUFFER_LIFETIME_OBJECTS := $(patsubst %.o,$(TEST_DIR)/san-lifetime/%.o,$(filter-out src/nift.o src/CLI.o,$(CLI_OBJECTS)))
$(FILE_BUFFER_LIFETIME_GUARD): tests/v410_file_buffer_guard.cpp $(FILE_BUFFER_LIFETIME_OBJECTS) $(SAN_LIBFFI_A)
	$(CXX) $(SAN_CPPFLAGS) -std=c++17 -pthread $(SAN_LIFETIME_FLAGS) tests/v410_file_buffer_guard.cpp $(FILE_BUFFER_LIFETIME_OBJECTS) $(SAN_LIBFFI_A) $(LDLIBS) -o $@
.PHONY: test-v410-file-buffer-lifetime
test-v410-file-buffer-lifetime: $(SAN_LIFETIME_TARGET) $(FILE_BUFFER_LIFETIME_GUARD)
	$(LIFETIME_RUN) NIFT=$(abspath $(SAN_LIFETIME_TARGET)) $(PYTHON) tests/v410_file_buffer_parity.py
	$(LIFETIME_RUN) $(FILE_BUFFER_LIFETIME_GUARD)
test-sanitize-lifetime: test-v410-file-buffer-lifetime

.PHONY: test-v410-build-pipeline
test-v410-build-pipeline: $(TARGET)
	python3 tests/v410_build_pipeline.py --nift "$(CURDIR)/$(TARGET)"
test: test-v410-build-pipeline

.PHONY: test-v410-build-pipeline-lifetime
test-v410-build-pipeline-lifetime: $(SAN_LIFETIME_TARGET)
	$(LIFETIME_RUN) $(PYTHON) tests/v410_build_pipeline.py --nift "$(CURDIR)/$(SAN_LIFETIME_TARGET)"
test-sanitize-lifetime: test-v410-build-pipeline-lifetime

.PHONY: test-v410-build-pipeline test-v410-build-pipeline-lifetime

# Local v4.11 process hardening: isolated binaries carry deterministic faults;
# production objects never link test hooks. Works on native Windows and POSIX.
PROCESS_FAILURE_TEST := $(TEST_DIR)/process-failure$(EXEEXT)
PROCESS_CONTRACT_TEST := $(TEST_DIR)/process-contract$(EXEEXT)
$(PROCESS_FAILURE_TEST): tests/process_failure.cpp src/Process.cpp src/JobControl.cpp src/Proc.h src/PreparedProcessPOSIX.h src/ProcessPOSIX.h src/ProcessEnvironment.h src/ProcessTestHooks.h
	@mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_PROCESS_TEST_HOOKS tests/process_failure.cpp src/Process.cpp src/JobControl.cpp -o $@
$(PROCESS_CONTRACT_TEST): tests/process_contract.cpp src/Process.cpp src/Proc.h src/PreparedProcessPOSIX.h src/ProcessPOSIX.h src/ProcessEnvironment.h src/ProcessTestHooks.h
	@mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_PROCESS_TEST_HOOKS tests/process_contract.cpp src/Process.cpp -o $@
test-process-hardening: $(PROCESS_FAILURE_TEST) $(PROCESS_CONTRACT_TEST)
	python3 tests/process_hardening.py --failure $(PROCESS_FAILURE_TEST) --contract $(PROCESS_CONTRACT_TEST) --output $(TEST_DIR)/process-hardening.json
test: test-process-hardening
.PHONY: test-process-hardening

test-randomized-incremental: $(TARGET)
	python3 tests/randomized_incremental_differential.py --nift $(TARGET)
.PHONY: test-randomized-incremental

.PHONY: test-consumer-snapshots
test: test-consumer-snapshots
test-consumer-snapshots: $(TARGET)
	mkdir -p .build
	python3 tests/shared_dependency_targeted_reproducer.py --nift ./$(TARGET) --require-clean --output .build/consumer-snapshot-matrix.json
	python3 tests/consumer_dependency_snapshots.py --nift ./$(TARGET) --output .build/consumer-snapshot-contracts.json

# One-comparison following status: native precision, errors and mutation boundaries.
.PHONY: test-dependency-status
test: test-dependency-status
test-dependency-status: $(TARGET) src/FileSystem.o
	@mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/dependency_status.cpp src/FileSystem.o -o $(TEST_DIR)/dependency-status-test$(EXEEXT)
	$(TEST_DIR)/dependency-status-test$(EXEEXT) $(TEST_DIR)/dependency-status-fixture
	$(PYTHON) tests/dependency_status.py --nift ./$(TARGET) --output $(TEST_DIR)/dependency-status-contracts.json

.PHONY: test-dependency-path-identity
test: test-dependency-path-identity
test-dependency-path-identity: $(TARGET)
	@mkdir -p $(TEST_DIR)
	$(PYTHON) tests/dependency_path_identity.py --nift ./$(TARGET) --output $(TEST_DIR)/dependency-path-identity.json

.PHONY: test-sort-number-compact
test: test-sort-number-compact
test-sort-number-compact: $(TARGET)
	python3 tests/sort_number_compact.py --nift ./$(TARGET)
	python3 tests/sort_number_compact_oracle.py

.PHONY: test-evaluated-operand
$(TEST_DIR)/evaluated-operand-unit$(EXEEXT): tests/evaluated_operand_unit.cpp src/EvaluatedOperand.h src/Ast.cpp src/Ast.h src/RuntimeValue.cpp src/RuntimeValue.h src/SourceView.h
	mkdir -p $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/evaluated_operand_unit.cpp src/Ast.cpp src/RuntimeValue.cpp -o $@
test-evaluated-operand: $(TEST_DIR)/evaluated-operand-unit$(EXEEXT) $(TARGET)
	$(TEST_DIR)/evaluated-operand-unit$(EXEEXT)
	NIFT_BIN="$(CURDIR)/$(TARGET)" python3 tests/evaluated_operand_smoke.py
test: test-evaluated-operand
.PHONY: test-evaluated-operand-differential
test-evaluated-operand-differential: $(TARGET)
	@test -n "$(NIFT_BASELINE_BIN)" || { echo "NIFT_BASELINE_BIN is required" >&2; exit 1; }
	python3 tests/evaluated_operand_differential.py --baseline "$(NIFT_BASELINE_BIN)" --candidate "$(CURDIR)/$(TARGET)"

# v4.12 function-call semantic certification (permanent).
.PHONY: test-call-semantics
test-call-semantics: $(TARGET)
	python3 tests/v412_nested_index_oracle.py "$(CURDIR)/$(TARGET)"
	python3 tests/v412_method_error_oracle.py --negative-control "$(CURDIR)/$(TARGET)"
	python3 tests/v412_call_semantic_contract.py --candidate "$(CURDIR)/$(TARGET)"
	python3 tests/v412_method_error_oracle.py "$(CURDIR)/$(TARGET)"
.PHONY: test-release-cert
test-release-cert: $(TARGET)
	python3 scripts/release_certify.py --root "$(CURDIR)" --jobs "$(shell nproc)"

test: test-call-semantics

# Runtime 2.0 CP14 built-in natives (src/rt/natives): unit tests, randomised
# differential test against the legacy engine, context/shape probe of legacy,
# ASan+UBSan build. Linked against the CP09 value model only (not the CP12
# compiler/VM). RT_BUILTINS_LEGACY: the legacy nift binary used as oracle.
RT_BUILTINS_LEGACY ?= $(CURDIR)/$(TARGET)
RT_BUILTINS_CORE := $(addprefix src/rt/,value.cpp string.cpp aggregate.cpp atom.cpp heap_account.cpp compare.cpp \
	number.cpp decimal.cpp render.cpp error.cpp handle.cpp legacy_bridge.cpp) src/RuntimeValue.cpp \
	src/Sha256.cpp src/JsonSchema.cpp
RT_BUILTINS_SOURCES := $(wildcard src/rt/natives/*.cpp)
# CP16: the native table includes the Host built-ins, which call the Host layer
# and the RuntimeValue-free helpers in ParserHostHelpers.cpp (never
# ParserHelpers.cpp, which needs the legacy value model). Every builtins binary,
# including the isolated bench below, links them.
RT_BUILTINS_HOST := $(RT_HOST_LINK_SOURCES) src/ParserHostHelpers.cpp src/Process.cpp src/FileSystem.cpp
RT_BUILTINS_CORE += $(RT_BUILTINS_HOST)
RT_BUILTINS_DEPS := $(RT_BUILTINS_SOURCES) $(wildcard src/rt/natives/*.h src/rt/natives/*.inc) \
	$(RT_BUILTINS_HOST) src/ParserHostHelpers.h \
	$(RT_BUILTINS_CORE) $(wildcard src/rt/*.h) src/RuntimeValue.h tests/rt/rt_test.h tests/rt/builtins_support.h
RT_BUILTINS_UNIT := $(TEST_DIR)/rt/builtins-unit$(EXEEXT)
RT_BUILTINS_DIFF := $(TEST_DIR)/rt/builtins-diff$(EXEEXT)

$(RT_BUILTINS_UNIT): tests/rt/builtins_unit.cpp $(RT_BUILTINS_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/builtins_unit.cpp $(RT_BUILTINS_SOURCES) $(RT_BUILTINS_CORE) -o "$@"

$(RT_BUILTINS_DIFF): tests/rt/builtins_diff.cpp $(RT_BUILTINS_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/builtins_diff.cpp $(RT_BUILTINS_SOURCES) $(RT_BUILTINS_CORE) -o "$@"

# ASCII case-conversion fast path (DeepSeek, D-044): all 256 bytes, the
# inline/heap boundary, 1 MiB input, locale independence, unchanged errors.
RT_CASE_UNIT := $(TEST_DIR)/rt/case-conversion-unit$(EXEEXT)
RT_CASE_BENCH := $(TEST_DIR)/rt/case-conversion-bench$(EXEEXT)

$(RT_CASE_UNIT): tests/rt/case_conversion_unit.cpp $(RT_BUILTINS_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/case_conversion_unit.cpp $(RT_BUILTINS_SOURCES) $(RT_BUILTINS_CORE) -o "$@"

$(RT_CASE_BENCH): tests/rt/case_conversion_bench.cpp $(RT_BUILTINS_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/rt/case_conversion_bench.cpp $(RT_BUILTINS_SOURCES) $(RT_BUILTINS_CORE) -o "$@"

.PHONY: test-rt-case-conversion test-rt-case-conversion-negative bench-rt-case-conversion
test-rt-case-conversion: $(RT_CASE_UNIT)
	"$(RT_CASE_UNIT)"

# Permanent negative control: the unmutated test must pass and a planted wrong
# ASCII mapping must make it exit nonzero (a mutant build failure is an error).
test-rt-case-conversion-negative:
	RT_BUILTINS_CORE="$(RT_BUILTINS_CORE)" RT_BUILTINS_SOURCES="$(RT_BUILTINS_SOURCES)" \
		CXX="$(CXX)" CXXFLAGS="$(CXXFLAGS)" bash tests/rt/case_conversion_negative.sh

# Both run in `make test`, so `make certify` covers them on every candidate.
test: test-rt-case-conversion test-rt-case-conversion-negative

bench-rt-case-conversion: $(RT_CASE_BENCH)
	"$(RT_CASE_BENCH)"

# Natives must not convert to legacy values: the bench binary links the
# natives with the CP09 value model WITHOUT RuntimeValue.cpp, legacy_bridge.cpp
# and render.cpp (a call into them fails the link), and the sources must not
# include their headers.
RT_BUILTINS_ISOLATED_CORE := $(addprefix src/rt/,value.cpp string.cpp aggregate.cpp atom.cpp heap_account.cpp \
	compare.cpp number.cpp decimal.cpp error.cpp handle.cpp) src/Sha256.cpp src/JsonSchema.cpp $(RT_BUILTINS_HOST)
RT_BUILTINS_BENCH := $(TEST_DIR)/rt/builtins-bench$(EXEEXT)

$(RT_BUILTINS_BENCH): tests/rt/builtins_bench.cpp $(RT_BUILTINS_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) -Isrc -Iinclude $(CXXFLAGS) -g tests/rt/builtins_bench.cpp $(RT_BUILTINS_SOURCES) $(RT_BUILTINS_ISOLATED_CORE) -o "$@"

# CP16 certification fix: secure_random_bytes checks the budget before its
# buffer exists and keeps one result-sized buffer. Linked with the isolated core
# (no RuntimeValue/legacy_bridge/render): a Host native that needs the legacy
# value model fails this link. NIFT_HOST_MUTANT=13 must fail the run.
RT_HOST_RANDOM_BUDGET := $(TEST_DIR)/rt/host-random-budget$(EXEEXT)
$(RT_HOST_RANDOM_BUDGET): tests/rt/host_random_budget.cpp $(RT_BUILTINS_DEPS)
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) -Isrc -Iinclude -Itests/rt $(CXXFLAGS) tests/rt/host_random_budget.cpp $(RT_BUILTINS_SOURCES) $(RT_BUILTINS_ISOLATED_CORE) -o "$@"

.PHONY: test-rt-host-random-budget
test-rt-host-random-budget: $(RT_HOST_RANDOM_BUDGET)
	"$(RT_HOST_RANDOM_BUDGET)"
test: test-rt-host-random-budget

.PHONY: test-rt-builtins-isolation test-rt-builtins-guards
test-rt-builtins-isolation: $(RT_BUILTINS_BENCH)
	@if grep -nE '#include .*(RuntimeValue|legacy_bridge|rt/render)|nift::RuntimeValue|(to|from)_legacy\(|append_rendered' \
		src/rt/natives/*; then \
		echo "FAIL: a native refers to the legacy value bridge" >&2; exit 1; fi
	@echo "PASS natives link without RuntimeValue/legacy_bridge/render and do not include them"

# Per-native instruction guards (callgrind; sequential, ~30 s): fails when a
# workload exceeds its locked Ir/call by more than 10 % at any size or its
# scaling exponent grows (tests/runtime2/builtins/builtin_guards.json).
test-rt-builtins-guards: $(RT_BUILTINS_BENCH)
	python3 tests/runtime2/builtins/builtins_guards.py "$(RT_BUILTINS_BENCH)" \
		--check tests/runtime2/builtins/builtin_guards.json

.PHONY: test-rt-builtins test-rt-builtins-sanitize test-rt-builtins-probe
# RT_BUILTINS_SEED / RT_BUILTINS_CASES vary the differential test.
test-rt-builtins: $(RT_BUILTINS_UNIT) $(RT_BUILTINS_DIFF) test-rt-builtins-isolation test-rt-case-conversion test-rt-case-conversion-negative test-rt-host-random-budget
	@test -x "$(RT_BUILTINS_LEGACY)" || { echo "legacy oracle $(RT_BUILTINS_LEGACY) missing (set RT_BUILTINS_LEGACY)" >&2; exit 1; }
	"$(RT_BUILTINS_UNIT)"
	"$(RT_BUILTINS_DIFF)" "$(RT_BUILTINS_LEGACY)" $${RT_BUILTINS_SEED:-20261018} $${RT_BUILTINS_CASES:-5000}
	python3 tools/rt-lint.py src/rt/natives
	python3 tests/runtime2/builtins/context_probe.py --nift "$(RT_BUILTINS_LEGACY)" \
		--expect-file tests/runtime2/builtins/context_dependent.txt

# Legacy's context- and shape-dependent built-in behaviour (D-015 inventory).
test-rt-builtins-probe:
	python3 tests/runtime2/builtins/context_probe.py --nift "$(RT_BUILTINS_LEGACY)" \
		--expect-file tests/runtime2/builtins/context_dependent.txt

# ASan+UBSan build of the built-in tests; every CP09 mutation entry point
# relocates storage, so a reference held across a mutation aborts.
test-rt-builtins-sanitize:
	mkdir -p "$(TEST_DIR)/rt-san"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/builtins_unit.cpp $(RT_BUILTINS_SOURCES) $(RT_BUILTINS_CORE) -o "$(TEST_DIR)/rt-san/builtins-unit"
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/builtins_diff.cpp $(RT_BUILTINS_SOURCES) $(RT_BUILTINS_CORE) -o "$(TEST_DIR)/rt-san/builtins-diff"
	"$(TEST_DIR)/rt-san/builtins-unit"
	"$(TEST_DIR)/rt-san/builtins-diff" "$(RT_BUILTINS_LEGACY)" $${RT_BUILTINS_SEED:-20261018} $${RT_BUILTINS_SAN_CASES:-2000}
	$(CXX) $(CPPFLAGS) -Itests/rt $(RT_SANITIZE_FLAGS) tests/rt/case_conversion_unit.cpp $(RT_BUILTINS_SOURCES) $(RT_BUILTINS_CORE) -o "$(TEST_DIR)/rt-san/case-conversion-unit"
	"$(TEST_DIR)/rt-san/case-conversion-unit"

# Loop-literal regression (v4.12.0 regressions 55cb323c, 5fe38bd1): string
# literals, escapes, interpolation and output in top-level, loop and
# loop-called-function code must match the pinned v4.6.0 output.
.PHONY: test-loop-literals
test-loop-literals: $(TARGET)
	python3 tests/runtime2/loop-literals/run.py --nift ./$(TARGET)
test: test-loop-literals

.PHONY: test-runtime2-literals
test-runtime2-literals: $(TARGET)
	python3 tests/runtime2/literals/run.py --nift ./$(TARGET) --oracle ./$(TARGET)
	python3 tests/runtime2/literals/decimal_infer.py --nift ./$(TARGET) --oracle ./$(TARGET)
test: test-runtime2-literals

.PHONY: test-runtime2-literals-scaling test-runtime2-literals-mutants test-runtime2-literals-decimal-negative
test-runtime2-literals-scaling: $(TARGET)
	python3 tests/runtime2/literals/scaling.py --nift ./$(TARGET) --runs 3

test-runtime2-literals-mutants: $(TARGET)
	bash tests/runtime2/literals/negative_control.sh "$(CURDIR)/$(TARGET)"

test-runtime2-literals-f1-negative: $(TARGET)
	bash tests/runtime2/literals/f1_negative_control.sh "$(CURDIR)/$(TARGET)"

test-runtime2-literals-decimal-negative: $(TARGET)
	bash tests/runtime2/literals/decimal_infer_negative_control.sh "$(CURDIR)/$(TARGET)"

# The same suite against the ASan/UBSan build of nift (modified prepared-path
# literal and output evaluation under the sanitizers).
.PHONY: test-loop-literals-sanitize
test-loop-literals-sanitize: $(SAN_TARGET)
	python3 tests/runtime2/loop-literals/run.py --nift "$(SAN_TARGET)"

# CP12c slice 0: the inventory regression and its test-only negative control.
# The mutant inventories the unlinked source on purpose; normal builds contain
# none of that code (the macro is only on this object).
INVENTORY_MUTANT_BIN := $(TEST_DIR)/nift-inventory-mutants$(EXEEXT)
INVENTORY_MUTANT_OBJECT := $(TEST_DIR)/inventory-mutants/refusal_inventory.o
$(INVENTORY_MUTANT_OBJECT): src/rt/refusal_inventory.cpp $(RT_VALUE_HEADERS)
	mkdir -p "$(TEST_DIR)/inventory-mutants"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_INVENTORY_MUTANT=1 -c $< -o $@
$(INVENTORY_MUTANT_BIN): $(filter-out src/rt/refusal_inventory.o,$(CLI_OBJECTS)) $(INVENTORY_MUTANT_OBJECT) $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/rt/refusal_inventory.o,$(CLI_OBJECTS)) $(INVENTORY_MUTANT_OBJECT) \
		$(LDLIBS) -o $@
.PHONY: test-runtime2-inventory-links
test-runtime2-inventory-links: $(TARGET) $(INVENTORY_MUTANT_BIN)
	python3 tests/runtime2/vm_inventory_links.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/runtime2/vm_inventory_links.py --nift "$(CURDIR)/$(INVENTORY_MUTANT_BIN)" --mutant
test-rt-vm: test-runtime2-inventory-links

# Negative control for the shared module-host-op refusal (rt/module_host_op.h):
# a build whose inventory alone skips the check must fail the inventory-link
# suite on exactly the module-callable case (runner and inventory disagree).
INVENTORY_HOST_MUTANT_BIN := $(TEST_DIR)/nift-inventory-host-mutant$(EXEEXT)
INVENTORY_HOST_MUTANT_OBJECT := $(TEST_DIR)/inventory-host-mutant/refusal_inventory.o
$(INVENTORY_HOST_MUTANT_OBJECT): src/rt/refusal_inventory.cpp src/rt/module_host_op.h $(RT_VALUE_HEADERS)
	mkdir -p "$(TEST_DIR)/inventory-host-mutant"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_INVENTORY_HOST_MUTANT=1 -c $< -o $@
$(INVENTORY_HOST_MUTANT_BIN): $(filter-out src/rt/refusal_inventory.o,$(CLI_OBJECTS)) $(INVENTORY_HOST_MUTANT_OBJECT) $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/rt/refusal_inventory.o,$(CLI_OBJECTS)) $(INVENTORY_HOST_MUTANT_OBJECT) \
		$(LDLIBS) -o $@
.PHONY: test-runtime2-inventory-host-mutant
test-runtime2-inventory-host-mutant: $(TARGET) $(INVENTORY_HOST_MUTANT_BIN)
	bash tests/runtime2/inventory_host_mutant.sh "$(CURDIR)/$(TARGET)" "$(CURDIR)/$(INVENTORY_HOST_MUTANT_BIN)"
test-rt-vm: test-runtime2-inventory-host-mutant
# --- CP20 shell spawn: focused suite and compiling negative controls ---------
CP20_MUTANT_NIFTS := $(foreach m,1 2 3 4 5 6 7 8 9 11 12,$(TEST_DIR)/cp20-mutants/$(m)/nift$(EXEEXT))
$(TEST_DIR)/cp20-mutants/%/CLI.o: src/CLI.cpp src/shell/CommandDispatch.h src/shell/Spawn.h
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_CP20_MUTANT=$* -MMD -MP -c src/CLI.cpp -o "$@"
$(TEST_DIR)/cp20-mutants/%/Spawn.o: src/shell/Spawn.cpp src/shell/Spawn.h
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_CP20_MUTANT=$* -MMD -MP -c src/shell/Spawn.cpp -o "$@"
$(TEST_DIR)/cp20-mutants/%/nift$(EXEEXT): $(filter-out src/CLI.o src/shell/Spawn.o,$(CLI_OBJECTS)) $(TEST_DIR)/cp20-mutants/%/CLI.o $(TEST_DIR)/cp20-mutants/%/Spawn.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/CLI.o src/shell/Spawn.o,$(CLI_OBJECTS)) $(TEST_DIR)/cp20-mutants/$*/CLI.o $(TEST_DIR)/cp20-mutants/$*/Spawn.o $(LDLIBS) -o "$@"
.PHONY: test-cp20-shell-spawn test-cp20-shell-spawn-mutants
test-cp20-shell-spawn: $(TARGET)
	python3 tests/cp20_shell_spawn.py --nift "$(CURDIR)/$(TARGET)"
test-cp20-shell-spawn-mutants: $(TARGET) $(CP20_MUTANT_NIFTS)
	@for m in 1 2 3 4 5 6 7 8 9 11 12; do \
	  bin="$(CURDIR)/$(TEST_DIR)/cp20-mutants/$$m/nift$(EXEEXT)"; \
	  if [ "$$m" = "11" ]; then \
	    python3 tests/cp20_fd_leak.py "$$bin" 10 > ".build/cp20-mutants/$$m.log" 2>&1; rc=$$?; \
	  else \
	    python3 tests/cp20_shell_spawn.py --nift "$$bin" > ".build/cp20-mutants/$$m.log" 2>&1; rc=$$?; \
	  fi; \
	  case "$$m" in \
	    1) pattern="FAIL streaming-plain" ;; \
	    2) pattern="FAIL stdout-append" ;; \
	    3) pattern="FAIL merge-pipe" ;; \
	    4) pattern="FAIL env-scope" ;; \
	    5) pattern="FAIL background-leftover" ;; \
	    6) pattern="FAIL pipeline-last-status" ;; \
	    7) pattern="FAIL background-zombie" ;; \
	    8) pattern="FAIL script-bg-state" ;; \
	    9) pattern="FAIL script-stopped" ;; \
	    11) pattern="RESULT LEAK" ;; \
	    12) pattern="FAIL stderr-file" ;; \
	  esac; \
	  if [ $$rc -eq 0 ] || ! grep -q "$$pattern" ".build/cp20-mutants/$$m.log"; then \
	    echo "FAIL CP20 mutant $$m: not detected"; tail -20 ".build/cp20-mutants/$$m.log"; exit 1; \
	  fi; \
	  echo "PASS CP20 mutant $$m: rejected"; \
	done
.PHONY: test-cp20-fd-leak
test-cp20-fd-leak: $(TARGET)
	$(PYTHON) tests/cp20_fd_leak.py "$(CURDIR)/$(TARGET)" 20
.PHONY: test-cp20-launch-bench
test-cp20-launch-bench: $(TARGET)
	python3 tests/cp20_launch_bench.py --nift "$(CURDIR)/$(TARGET)"
.PHONY: test-cp20-r1 test-cp20-r1-flake test-cp20-r1-windows
test-cp20-r1: $(TARGET)
	python3 tests/cp20_shell_spawn.py --nift "$(CURDIR)/$(TARGET)"
	python3 tests/cp20_r1.py --nift "$(CURDIR)/$(TARGET)"
test-cp20-r1-flake: $(TARGET)
	@for i in $$(seq 1 20); do \
	  python3 tests/cp20_r1.py --nift "$(CURDIR)/$(TARGET)" > "$(TEST_DIR)/cp20-r1-pty-$$i.log" 2>&1 || { echo "FAIL PTY round $$i"; cat "$(TEST_DIR)/cp20-r1-pty-$$i.log"; exit 1; }; \
	done; echo "PASS CP20 PTY suite 20/20"
test-cp20-r1-windows: $(TARGET)
	$(PYTHON) tests/cp20_windows_pipeline.py "$(CURDIR)/$(TARGET)" $(V44_SKIP_77)
# CP20 R3 certification integration (after the CP16 rebase): required CP20
# suites/mutants are Runtime 2.0 gates; the Windows probe is a CI target whose
# POSIX run exits 77 and is skipped by $(V44_SKIP_77).
CERTIFY_RUNTIME2_GATES += test-cp20-shell-spawn test-cp20-shell-spawn-mutants test-cp20-r1 test-v45-native-runtime
CERTIFY_CI_TARGETS += test-cp20-r1-windows
CERTIFY_RUNTIME2_GATES += test-cp20-fd-leak
# CP20 R3: Host-side exec-report fd-leak negative control (M11 host variant).
CP20_HOST_FD_MUTANT := $(TEST_DIR)/cp20-mutants/host11/nift$(EXEEXT)
$(TEST_DIR)/cp20-mutants/host11/process_posix_launch.o: src/rt/host/process_posix_launch.cpp src/rt/host/process_posix_launch.h
	mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_CP20_MUTANT=11 -MMD -MP -c $< -o "$@"
$(CP20_HOST_FD_MUTANT): $(filter-out src/rt/host/process_posix_launch.o,$(CLI_OBJECTS)) \
	$(TEST_DIR)/cp20-mutants/host11/process_posix_launch.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/rt/host/process_posix_launch.o,$(CLI_OBJECTS)) \
		$(TEST_DIR)/cp20-mutants/host11/process_posix_launch.o $(LDLIBS) -o "$@"
.PHONY: test-cp20-host-fd-mutant
test-cp20-host-fd-mutant: $(TARGET) $(CP20_HOST_FD_MUTANT)
	$(PYTHON) tests/cp20_fd_leak.py "$(CURDIR)/$(TARGET)" 20
	@log="$(TEST_DIR)/cp20-mutants/host11/fd-leak.log"; \
	  $(PYTHON) tests/cp20_fd_leak.py "$(CURDIR)/$(CP20_HOST_FD_MUTANT)" 10 >"$$log" 2>&1; rc=$$?; \
	  if [ "$$rc" -ne 1 ] || ! grep -qx 'RESULT LEAK' "$$log" || \
	     ! grep -Eq '^fds before=[0-9]+ after=[0-9]+ delta=[1-9][0-9]* .*zombies_after=0$$' "$$log" || \
	     grep -Eq 'Traceback|AssertionError|TimeoutExpired' "$$log"; then \
	    echo "FAIL CP20 host fd mutant: expected descriptor-leak assertion (rc=$$rc)"; cat "$$log"; exit 1; \
	  fi; echo "PASS CP20 host fd mutant: descriptor leak detected"
CERTIFY_RUNTIME2_GATES += test-cp20-host-fd-mutant
certify-targets: test-cp20-shell-spawn test-cp20-shell-spawn-mutants test-cp20-r1 test-v45-native-runtime test-cp20-fd-leak test-cp20-r1-windows test-cp20-host-fd-mutant
# CP12C Slice 1: closure fixtures.  Phase A runs --prepare (legacy oracle +
# current refusal); Phase B runs the differential mode and the mutant gate.
.PHONY: test-runtime2-closures-s1-prepare test-runtime2-closures-s1
test-runtime2-closures-s1-prepare: $(TARGET)
	python3 tests/runtime2/closures_s1.py --nift "$(CURDIR)/$(TARGET)" --prepare
test-runtime2-closures-s1: $(TARGET)
	python3 tests/runtime2/closures_s1.py --nift "$(CURDIR)/$(TARGET)"
.PHONY: test-runtime2-closures-s1-stress-prepare test-runtime2-closures-s1-stress
test-runtime2-closures-s1-stress-prepare: $(TARGET)
	python3 tests/runtime2/closures_s1_stress.py --nift "$(CURDIR)/$(TARGET)" --prepare
test-runtime2-closures-s1-stress: $(TARGET)
	python3 tests/runtime2/closures_s1_stress.py --nift "$(CURDIR)/$(TARGET)"

# CP12C Slice 1 R6: widened kind-set bits and their compiling alias mutant.
KIND_BITS_UNIT := $(TEST_DIR)/rt/kind-bits$(EXEEXT)
$(KIND_BITS_UNIT): tests/rt/kind_bits.cpp src/rt/infer.h
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/kind_bits.cpp -o "$@"
KIND_BITS_MUTANT := $(TEST_DIR)/rt/kind-bits-mutant$(EXEEXT)
$(KIND_BITS_MUTANT): tests/rt/kind_bits.cpp src/rt/infer.h
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) -DNIFT_KIND_MUTANT=1 tests/rt/kind_bits.cpp -o "$@"
.PHONY: test-runtime2-kind-bits
test-runtime2-kind-bits: $(KIND_BITS_UNIT) $(KIND_BITS_MUTANT)
	"$(KIND_BITS_UNIT)"
	! "$(KIND_BITS_MUTANT)"

# CP12C Slice 1 C1-A: rejected cyclic cell stores, atomicity and no leak.
CLOSURE_CYCLE_STRESS := $(TEST_DIR)/rt/closure-cycle-stress$(EXEEXT)
$(CLOSURE_CYCLE_STRESS): tests/rt/closure_cycle_stress.cpp $(RT_VALUE_SOURCES) $(RT_VALUE_HEADERS) src/RuntimeValue.cpp
	mkdir -p "$(TEST_DIR)/rt"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) tests/rt/closure_cycle_stress.cpp $(RT_VALUE_SOURCES) src/RuntimeValue.cpp -o "$@"
.PHONY: test-runtime2-closure-cycle-stress
test-runtime2-closure-cycle-stress: $(CLOSURE_CYCLE_STRESS)
	"$(CLOSURE_CYCLE_STRESS)"
test-runtime2-closures-s1-stress: test-runtime2-closure-cycle-stress

# CP12C Slice 1 negative controls: one compiling binary per deliberate defect.
CLOSURE_MUTANT ?= 0
CLOSURE_MUTANT_BIN := $(TEST_DIR)/closure-mutants/nift-$(CLOSURE_MUTANT)
CLOSURE_MUTANT_DIR := $(TEST_DIR)/closure-mutants/$(CLOSURE_MUTANT)
$(CLOSURE_MUTANT_DIR)/vm.o: src/rt/vm.cpp $(RT_VALUE_HEADERS)
	mkdir -p "$(CLOSURE_MUTANT_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_CLOSURE_MUTANT=$(CLOSURE_MUTANT) -c $< -o $@
$(CLOSURE_MUTANT_DIR)/value.o: src/rt/value.cpp $(RT_VALUE_HEADERS)
	mkdir -p "$(CLOSURE_MUTANT_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_CLOSURE_MUTANT=$(CLOSURE_MUTANT) -c $< -o $@
$(CLOSURE_MUTANT_DIR)/vm_closure.o: src/rt/vm_closure.cpp $(RT_VALUE_HEADERS)
	mkdir -p "$(CLOSURE_MUTANT_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_CLOSURE_MUTANT=$(CLOSURE_MUTANT) -c $< -o $@
$(CLOSURE_MUTANT_BIN): $(filter-out src/rt/vm.o src/rt/value.o src/rt/vm_closure.o,$(CLI_OBJECTS)) \
		$(CLOSURE_MUTANT_DIR)/vm.o $(CLOSURE_MUTANT_DIR)/value.o $(CLOSURE_MUTANT_DIR)/vm_closure.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/rt/vm.o src/rt/value.o src/rt/vm_closure.o,$(CLI_OBJECTS)) \
		$(CLOSURE_MUTANT_DIR)/vm.o $(CLOSURE_MUTANT_DIR)/value.o $(CLOSURE_MUTANT_DIR)/vm_closure.o $(LDLIBS) -o $@
.PHONY: test-runtime2-closures-s1-mutants
test-runtime2-closures-s1-mutants: $(TARGET)
	python3 tests/runtime2/closures_s1.py --nift "$(CURDIR)/$(TARGET)"
	normal_sha=$$(sha256sum "$(CURDIR)/$(TARGET)" | cut -d' ' -f1); \
	tree=$$(git rev-parse HEAD^{tree}); \
	for n in 1 2 3 4 5 6; do \
		$(MAKE) CLOSURE_MUTANT=$$n "$(TEST_DIR)/closure-mutants/nift-$$n" >/dev/null || exit 1; \
		sha=$$(sha256sum "$(TEST_DIR)/closure-mutants/nift-$$n" | cut -d' ' -f1); \
		printf '{"mutant": %s, "sha256": "%s", "build": "closure-mutant", "normal_sha256": "%s", "source_tree": "%s"}\n' "$$n" "$$sha" "$$normal_sha" "$$tree" > "$(TEST_DIR)/closure-mutants/nift-$$n.json"; \
		python3 tests/runtime2/closures_s1.py --nift "$(CURDIR)/$(TEST_DIR)/closure-mutants/nift-$$n" \
			--normal "$(CURDIR)/$(TARGET)" --mutant-info "$(CURDIR)/$(TEST_DIR)/closure-mutants/nift-$$n.json" \
			--mutant $$n || exit 1; \
	done
	for n in 7 8; do \
		$(MAKE) CLOSURE_MUTANT=$$n "$(TEST_DIR)/closure-mutants/nift-$$n" >/dev/null || exit 1; \
		python3 tests/runtime2/closures_s1_stress.py --nift "$(CURDIR)/$(TEST_DIR)/closure-mutants/nift-$$n" --mutant || exit 1; \
	done
	python3 tests/runtime2/closures_s1_mutant_harness.py --nift "$(CURDIR)/$(TARGET)"
test-rt-vm: test-runtime2-kind-bits test-runtime2-closures-s1 test-runtime2-closures-s1-stress test-runtime2-closures-s1-mutants test-runtime2-cp12c-mutant-controls

# CP12C slice 2: default `for` differential fixtures, mutants and the
# memory/performance measurement (compiler_loops.cpp + vm_iter.cpp mutants).
.PHONY: test-runtime2-for-s2 test-runtime2-for-s2-mutants test-runtime2-for-s2-sanitize
test-runtime2-for-s2: $(TARGET)
	python3 tests/runtime2/for_s2.py --nift "$(CURDIR)/$(TARGET)"
test-runtime2-for-s2-sanitize: $(SAN_TARGET)
	python3 tests/runtime2/for_s2.py --nift "$(CURDIR)/$(SAN_TARGET)" --skip-million

FOR_MUTANT ?= 0
FOR_MUTANT_BIN := $(TEST_DIR)/for-mutants/nift-$(FOR_MUTANT)
FOR_MUTANT_DIR := $(TEST_DIR)/for-mutants/$(FOR_MUTANT)
$(FOR_MUTANT_DIR)/compiler_loops.o: src/rt/compiler_loops.cpp $(RT_VALUE_HEADERS)
	mkdir -p "$(FOR_MUTANT_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_FOR_MUTANT=$(FOR_MUTANT) -c $< -o $@
$(FOR_MUTANT_DIR)/compiler_loops_step.o: src/rt/compiler_loops_step.cpp $(RT_VALUE_HEADERS)
	mkdir -p "$(FOR_MUTANT_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_FOR_MUTANT=$(FOR_MUTANT) -c $< -o $@
$(FOR_MUTANT_DIR)/compiler_loops_bind.o: src/rt/compiler_loops_bind.cpp $(RT_VALUE_HEADERS)
	mkdir -p "$(FOR_MUTANT_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_FOR_MUTANT=$(FOR_MUTANT) -c $< -o $@
$(FOR_MUTANT_DIR)/vm_iter.o: src/rt/vm_iter.cpp $(RT_VALUE_HEADERS)
	mkdir -p "$(FOR_MUTANT_DIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_FOR_MUTANT=$(FOR_MUTANT) -c $< -o $@
FOR_MUTANT_OBJECTS := src/rt/compiler_loops.o src/rt/compiler_loops_step.o src/rt/compiler_loops_bind.o src/rt/vm_iter.o
$(FOR_MUTANT_BIN): $(filter-out $(FOR_MUTANT_OBJECTS),$(CLI_OBJECTS)) \
		$(FOR_MUTANT_DIR)/compiler_loops.o $(FOR_MUTANT_DIR)/compiler_loops_step.o \
		$(FOR_MUTANT_DIR)/compiler_loops_bind.o $(FOR_MUTANT_DIR)/vm_iter.o $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out $(FOR_MUTANT_OBJECTS),$(CLI_OBJECTS)) \
		$(FOR_MUTANT_DIR)/compiler_loops.o $(FOR_MUTANT_DIR)/compiler_loops_step.o \
		$(FOR_MUTANT_DIR)/compiler_loops_bind.o $(FOR_MUTANT_DIR)/vm_iter.o $(LDLIBS) -o $@
.PHONY: test-runtime2-cp12c-probes
test-runtime2-cp12c-mutant-controls: $(TARGET)
	python3 tests/runtime2/cp12c_mutant_controls.py --normal "$(CURDIR)/$(TARGET)"
test-runtime2-cp12c-probes: $(TARGET)
	python3 tests/runtime2/cp12c_probes.py "$(CURDIR)/$(TARGET)" tests/runtime2/cp12c-probes/src .build/cp12c-probes-src
	python3 tests/runtime2/cp12c_probes.py "$(CURDIR)/$(TARGET)" tests/runtime2/cp12c-probes/src2 .build/cp12c-probes-src2
test: test-runtime2-cp12c-probes
test-runtime2-for-s2-mutants: $(TARGET)
	python3 tests/runtime2/for_s2.py --nift "$(CURDIR)/$(TARGET)"
	normal_sha=$$(sha256sum "$(CURDIR)/$(TARGET)" | cut -d' ' -f1); \
	tree=$$(git rev-parse HEAD^{tree}); \
	for n in 1 2 3 4 5 6 7; do \
		$(MAKE) FOR_MUTANT=$$n "$(TEST_DIR)/for-mutants/nift-$$n" >/dev/null || exit 1; \
		sha=$$(sha256sum "$(TEST_DIR)/for-mutants/nift-$$n" | cut -d' ' -f1); \
		printf '{"mutant": %s, "sha256": "%s", "build": "for-mutant", "normal_sha256": "%s", "source_tree": "%s"}\n' "$$n" "$$sha" "$$normal_sha" "$$tree" > "$(TEST_DIR)/for-mutants/nift-$$n.json"; \
		python3 tests/runtime2/for_s2.py --nift "$(CURDIR)/$(TEST_DIR)/for-mutants/nift-$$n" \
			--normal "$(CURDIR)/$(TARGET)" --mutant-info "$(CURDIR)/$(TEST_DIR)/for-mutants/nift-$$n.json" \
			--mutant $$n || exit 1; \
	done
test-rt-vm: test-runtime2-for-s2 test-runtime2-for-s2-mutants
test-runtime2-for-s2-bench: $(TARGET)
	python3 tests/runtime2/for_s2_bench.py --nift "$(CURDIR)/$(TARGET)"
.PHONY: test-runtime2-for-s2-known-deps
test-runtime2-for-s2-known-deps: $(TARGET)
	python3 tests/runtime2/for_s2_known_deps.py --nift "$(CURDIR)/$(TARGET)"

# CP12C R7: source-location identity versus iteration-slot ownership.
.PHONY: test-runtime2-capture-origins
test-runtime2-capture-origins: $(TARGET)
	python3 tests/runtime2/capture_origins.py --nift "$(CURDIR)/$(TARGET)"
test: test-runtime2-capture-origins
test-rt-vm: test-runtime2-capture-origins

# CP14B focused collection gates (accepted; wired into the hierarchy below).
CP14B_EVIDENCE ?= $(TEST_DIR)/cp14b/evidence
CP14B_BASE ?= $(CURDIR)/$(TARGET)
CP14B_ORACLE := tests/runtime2/cp14b/fixtures/d4-traces.json.gz
.PHONY: test-runtime2-cp14b test-runtime2-cp14b-sort
# The default legacy oracle is this binary's unchanged NIFT_VM=0 engine.
# Pass the accepted foundation binary as CP14B_BASE for an immutable comparison.
test-runtime2-cp14b: $(TARGET) $(CP13B_SWITCH_NIFT)
	mkdir -p "$(CP14B_EVIDENCE)"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/functional.py --nift "$(CURDIR)/$(TARGET)" --legacy "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/functional.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/functional.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --legacy "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/functional-switch.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/effects.py --nift "$(CURDIR)/$(TARGET)" --legacy "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/effects.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/effects.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --legacy "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/effects-switch.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/origins.py --nift "$(CURDIR)/$(TARGET)" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/origins.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/origins.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/origins-switch.json"
test-runtime2-cp14b-sort: $(TARGET) $(CP13B_SWITCH_NIFT)
	mkdir -p "$(CP14B_EVIDENCE)"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/sort_replay.py --nift "$(CURDIR)/$(TARGET)" --oracle "$(CP14B_ORACLE)" --output "$(CP14B_EVIDENCE)/sort.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/sort_replay.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --oracle "$(CP14B_ORACLE)" --output "$(CP14B_EVIDENCE)/sort-switch.json"

.PHONY: test-runtime2-cp14b-design-mutants
test-runtime2-cp14b-design-mutants: $(TARGET)
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/design_mutations.py --legacy "$(CP14B_BASE)" --out "$(CURDIR)/$(TEST_DIR)/cp14b/design-mutants"

# D-018c consumers require the independent parser even when invoked separately.
test-runtime2-rt-diff test-runtime2-rt-diff-selftest test-runtime2-closures-s1 test-runtime2-closures-s1-mutants test-runtime2-cp14b test-runtime2-cp13b: $(TEST_DIR)/rt-parse-dump$(EXEEXT)
.PHONY: test-rt-vm-policy-tooling
test-rt-vm-policy-tooling: $(TEST_DIR)/rt-parse-dump$(EXEEXT)
	python3 tests/runtime2/vm_policy_tooling_test.py
test-rt-vm: test-rt-vm-policy-tooling

# CP14B comparator profile (owner decision 2026-10-25): sort(comparator) runs
# on the VM only for the frozen GCC 15.2/libstdc++ 20260321 schedule; every
# comparator check first probes the binary (tests/runtime2/cp14b/
# comparator_profile.py) and cross-checks NIFT_CP14B_CXX. On any other profile
# it requires the exact method:sort refusal and legacy parity and prints
# "SKIP UNVERIFIED COMPARATOR COVERAGE" -- never comparator certification. The
# milestone run sets NIFT_CP14B_REQUIRE_COMPARATOR=1 on GCC 15.2, which makes a
# refused profile fail.
# CP14B permanent wiring (integration, 2026-10-25). Functional tier under
# test-rt-vm; ASan/UBSan, drain and allocation-failure tier under
# test-rt-vm-sanitize; named and design mutants in certification. Witnesses
# whose base is only the legacy oracle (NIFT_VM=0, unchanged under D-006) use
# this binary; delta witnesses against a pre-CP14B VM (inventory,
# context_matrix, comparator_contexts, reporting_regression) need an explicit
# accepted CP14B_BASE and are integration evidence, not permanent gates.
CP14B_LINUX_ONLY = if [ "$$(uname -s)" != Linux ]; then echo "SKIP CP14B $@: Linux/libstdc++ only (D4)"; exit 0; fi;
.PHONY: test-runtime2-cp14b-witnesses test-runtime2-cp14b-sanitize test-runtime2-cp14b-mutants
test-runtime2-cp14b-witnesses: $(TARGET) $(CP13B_SWITCH_NIFT)
	mkdir -p "$(CP14B_EVIDENCE)"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/push_admission.py --nift "$(CURDIR)/$(TARGET)" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/push-admission.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/copy_witnesses.py --nift "$(CURDIR)/$(TARGET)" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/copy-witnesses.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/copy_witnesses.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/copy-witnesses-switch.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/const_admission.py --nift "$(CURDIR)/$(TARGET)" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/const-admission.json"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/const_admission.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/const-admission-switch.json"
test-runtime2-cp14b-sanitize: $(TARGET)
	@$(CP14B_LINUX_ONLY) true
	mkdir -p "$(CP14B_EVIDENCE)"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/build_sanitizer.py --out "$(CURDIR)/$(TEST_DIR)/cp14b/san"
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/build_drain.py --out "$(CURDIR)/$(TEST_DIR)/cp14b/drain"
	set -e; for b in cp14b-san cp14b-san-switch; do \
	  bin="$(CURDIR)/$(TEST_DIR)/cp14b/san/$$b"; \
	  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/functional.py --nift "$$bin" --legacy "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/san-functional-$$b.json"; \
	  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/sort_replay.py --sanitizer --nift "$$bin" --legacy "$(CP14B_BASE)" --oracle "$(CP14B_ORACLE)" --output "$(CP14B_EVIDENCE)/san-sort-$$b.json"; \
	  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/origins.py --nift "$$bin" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/san-origins-$$b.json"; \
	  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/copy_witnesses.py --nift "$$bin" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/san-copy-$$b.json"; \
	  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/const_admission.py --vm-only-harness --nift "$$bin" --base "$(CP14B_BASE)" --output "$(CP14B_EVIDENCE)/san-const-$$b.json"; \
	done
test-runtime2-cp14b-mutants: $(TARGET)
	@$(CP14B_LINUX_ONLY) true
	NIFT_CP14B_CXX="$(CXX)" $(PYTHON) tests/runtime2/cp14b/mutations.py --nift "$(CURDIR)/$(TARGET)" --out "$(CURDIR)/$(TEST_DIR)/cp14b/mutants"
	$(MAKE) test-runtime2-cp14b-design-mutants
test-rt-vm: test-runtime2-cp14b
test-rt-vm: test-runtime2-cp14b-sort
test-rt-vm: test-runtime2-cp14b-witnesses
# D-040 exact witnesses (CP14B integration, owner 2026-10-25): negative controls
# for the shared vm_policies / rt-diff approvals of the six pinned programs.
.PHONY: test-runtime2-d040-witnesses
test-runtime2-d040-witnesses: $(TEST_DIR)/rt-parse-dump$(EXEEXT)
	$(PYTHON) tests/runtime2/d040_witnesses_selftest.py
test-rt-vm: test-runtime2-d040-witnesses
.PHONY: test-runtime2-cp14b-profile
test-runtime2-cp14b-profile:
	$(PYTHON) tests/runtime2/cp14b/comparator_profile_selftest.py
test-rt-vm: test-runtime2-cp14b-profile
test-rt-vm-sanitize: test-runtime2-cp14b-sanitize
certify-targets: test-runtime2-cp14b-mutants

# CP18 compiled-program interface: separate from legacy Engine execution.
CP18_C_TEST := $(TEST_DIR)/cp18/program$(EXEEXT)
$(CP18_C_TEST): tests/runtime2/cp18/program.c $(SHARED_LIB) include/nift/c_abi.h
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -pedantic $< -L. -lnift_c -pthread -Wl,-rpath,$(CURDIR) -o $@
.PHONY: test-runtime2-cp18
test-runtime2-cp18: $(CP18_C_TEST)
	$(CP18_C_TEST)
	$(PYTHON) tests/runtime2/cp18/python.py "$(CURDIR)/$(SHARED_LIB)"
	$(PYTHON) tests/runtime2/cp18/combined_cp14b.py "$(CURDIR)/$(SHARED_LIB)"
test-embed: test-runtime2-cp18

CP18_SWITCH_LIB := $(TEST_DIR)/cp18/switch/$(SHARED_LIB)
CP18_SWITCH_OBJ := $(TEST_DIR)/cp18/vm-switch.o
$(CP18_SWITCH_OBJ): src/rt/vm.cpp $(RT_VALUE_HEADERS)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_RT_VM_SWITCH -fPIC -c $< -o $@
$(CP18_SWITCH_LIB): $(C_ABI_PIC) $(CP18_SWITCH_OBJ) $(LIBFFI_A)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -shared $(LIBFFI_SHARED_LINK_FLAGS) -o $@ $(filter-out $(TEST_DIR)/pic/src/rt/vm.o,$(C_ABI_PIC)) $(CP18_SWITCH_OBJ) $(LIBFFI_A) $(SHARED_SYS_LIBS)
$(TEST_DIR)/cp18/program-switch$(EXEEXT): tests/runtime2/cp18/program.c $(CP18_SWITCH_LIB)
	$(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -pedantic $< $(abspath $(CP18_SWITCH_LIB)) -pthread -Wl,-rpath,$(abspath $(dir $(CP18_SWITCH_LIB))) -o $@
.PHONY: test-runtime2-cp18-switch
test-runtime2-cp18-switch: $(TEST_DIR)/cp18/program-switch$(EXEEXT)
	$(TEST_DIR)/cp18/program-switch$(EXEEXT)
	$(PYTHON) tests/runtime2/cp18/python.py "$(abspath $(CP18_SWITCH_LIB))"
	$(PYTHON) tests/runtime2/cp18/combined_cp14b.py "$(abspath $(CP18_SWITCH_LIB))"
test-runtime2-cp18: test-runtime2-cp18-switch
.PHONY: test-runtime2-cp18-sanitize
test-runtime2-cp18-sanitize:
	$(PYTHON) tests/runtime2/cp18/sanitize.py --out "$(CURDIR)/$(TEST_DIR)/cp18/sanitize"
test-embed: test-runtime2-cp18-sanitize

# CP18 R2: ordinary optimized allocation accounting, independent of LSan.
CP18_COW := $(TEST_DIR)/cp18/cow-allocation$(EXEEXT)
CP18_COW_SWITCH := $(TEST_DIR)/cp18/cow-allocation-switch$(EXEEXT)
$(CP18_COW): tests/runtime2/cp18/cow_allocation.cpp $(C_ABI_PIC) $(LIBFFI_A)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(C_ABI_PIC) $(LIBFFI_A) $(LDLIBS) -o $@
$(CP18_COW_SWITCH): tests/runtime2/cp18/cow_allocation.cpp $(C_ABI_PIC) $(CP18_SWITCH_OBJ) $(LIBFFI_A)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(filter-out $(TEST_DIR)/pic/src/rt/vm.o,$(C_ABI_PIC)) $(CP18_SWITCH_OBJ) $(LIBFFI_A) $(LDLIBS) -o $@
.PHONY: test-runtime2-cp18-cow
test-runtime2-cp18-cow: $(CP18_COW) $(CP18_COW_SWITCH) $(SHARED_LIB) $(CP18_SWITCH_LIB)
	$(CP18_COW)
	$(CP18_COW_SWITCH)
	$(PYTHON) tests/runtime2/cp18/cow_semantics.py "$(CURDIR)/$(SHARED_LIB)"
	$(PYTHON) tests/runtime2/cp18/cow_semantics.py "$(abspath $(CP18_SWITCH_LIB))"
	$(PYTHON) tests/runtime2/cp18/cow_negative.py --out "$(CURDIR)/$(TEST_DIR)/cp18/cow-negative" --libffi "$(CURDIR)/$(LIBFFI_A)"
test-runtime2-cp18: test-runtime2-cp18-cow

# CP19 persistent VM production witness (one VM and stable cells, no Parser bridge).
$(TEST_DIR)/cp19/session$(EXEEXT): tests/runtime2/cp19/session.cpp $(CP19_EXPR_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/session-switch$(EXEEXT): tests/runtime2/cp19/session.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
.PHONY: test-cp19-session
test-cp19-session: $(TEST_DIR)/cp19/session$(EXEEXT) $(TEST_DIR)/cp19/session-switch$(EXEEXT)
	$(TEST_DIR)/cp19/session$(EXEEXT)
	$(TEST_DIR)/cp19/session-switch$(EXEEXT)
$(TEST_DIR)/cp19/session-sanitize$(EXEEXT): tests/runtime2/cp19/session.cpp $(CP19_EXPR_SAN_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt $(SANITIZER_FLAGS) -std=c++17 -pthread $< $(CP19_EXPR_SAN_OBJECTS) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@
$(TEST_DIR)/cp19/session-switch-sanitize$(EXEEXT): tests/runtime2/cp19/session.cpp $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o
	@mkdir -p $(dir $@)
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt $(SANITIZER_FLAGS) -std=c++17 -pthread $< $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@
.PHONY: test-cp19-session-sanitize
test-cp19-session-sanitize: $(TEST_DIR)/cp19/session-sanitize$(EXEEXT) $(TEST_DIR)/cp19/session-switch-sanitize$(EXEEXT)
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 $(TEST_DIR)/cp19/session-sanitize$(EXEEXT)
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 $(TEST_DIR)/cp19/session-switch-sanitize$(EXEEXT)

# Real session mutation controls, each swapping one production object.
$(TEST_DIR)/cp19/session-control$(EXEEXT): tests/runtime2/cp19/session_mutants.cpp $(CP19_EXPR_OBJECTS)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/session-m1.o: src/rt/session.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_MUTANT=1 -MMD -MP -c $< -o $@
$(TEST_DIR)/cp19/session-m1$(EXEEXT): tests/runtime2/cp19/session_mutants.cpp $(filter-out src/rt/session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m1.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m1.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/session-m2.o: src/rt/session.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_MUTANT=2 -MMD -MP -c $< -o $@
$(TEST_DIR)/cp19/session-m2$(EXEEXT): tests/runtime2/cp19/session_mutants.cpp $(filter-out src/rt/session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m2.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m2.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/session-m3.o: src/rt/vm_session.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_MUTANT=3 -MMD -MP -c $< -o $@
$(TEST_DIR)/cp19/session-m3$(EXEEXT): tests/runtime2/cp19/session_mutants.cpp $(filter-out src/rt/vm_session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m3.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm_session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m3.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/session-m4.o: src/rt/session.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_MUTANT=4 -MMD -MP -c $< -o $@
$(TEST_DIR)/cp19/session-m4$(EXEEXT): tests/runtime2/cp19/session_mutants.cpp $(filter-out src/rt/session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m4.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m4.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/session-m5.o: src/rt/vm_closure.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_MUTANT=5 -MMD -MP -c $< -o $@
$(TEST_DIR)/cp19/session-m5$(EXEEXT): tests/runtime2/cp19/session_mutants.cpp $(filter-out src/rt/vm_closure.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m5.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm_closure.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m5.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/session-m6.o: src/rt/compiler_assign_expr.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_MUTANT=6 -MMD -MP -c $< -o $@
$(TEST_DIR)/cp19/session-m6$(EXEEXT): tests/runtime2/cp19/session_mutants.cpp $(filter-out src/rt/compiler_assign_expr.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m6.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/compiler_assign_expr.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m6.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/session-m7.o: src/rt/compiler_session_contract.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_MUTANT=7 -MMD -MP -c $< -o $@
$(TEST_DIR)/cp19/session-m7$(EXEEXT): tests/runtime2/cp19/session_mutants.cpp $(filter-out src/rt/compiler_session_contract.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m7.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/compiler_session_contract.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m7.o $(LDLIBS) -o $@
-include $(wildcard $(TEST_DIR)/cp19/session-m*.d)
.PHONY: test-cp19-session-mutants
test-cp19-session-mutants: $(TEST_DIR)/cp19/session-control$(EXEEXT) $(TEST_DIR)/cp19/session-m1$(EXEEXT) $(TEST_DIR)/cp19/session-m2$(EXEEXT) $(TEST_DIR)/cp19/session-m3$(EXEEXT) $(TEST_DIR)/cp19/session-m4$(EXEEXT) $(TEST_DIR)/cp19/session-m5$(EXEEXT) $(TEST_DIR)/cp19/session-m6$(EXEEXT) $(TEST_DIR)/cp19/session-m7$(EXEEXT)
	$(PYTHON) tests/runtime2/cp19/session_mutants.py --dir "$(CURDIR)/$(TEST_DIR)/cp19" --out "$(TEST_DIR)/cp19/session-mutants.json"
.PHONY: test-cp19-session-cli
test-cp19-session-cli: $(TARGET) $(CP13B_SWITCH_NIFT)
	$(PYTHON) tests/runtime2/cp19/session_cli.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/session-cli.jsonl"
	$(PYTHON) tests/runtime2/cp19/session_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/session-cli-switch.jsonl"
	$(PYTHON) tests/runtime2/cp19/session_refusal_cli.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/session-refusals.json"
	$(PYTHON) tests/runtime2/cp19/session_refusal_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/session-refusals-switch.json"
	$(PYTHON) tests/runtime2/cp19/session_classify_cli.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/session-classify.json"
	$(PYTHON) tests/runtime2/cp19/session_classify_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/session-classify-switch.json"
	$(PYTHON) tests/runtime2/cp19/session_render_cli.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/session-render.json"
	$(PYTHON) tests/runtime2/cp19/session_render_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/session-render-switch.json"
	$(PYTHON) tests/runtime2/cp19/session_callable_cli.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/session-callable.json"
	$(PYTHON) tests/runtime2/cp19/session_callable_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/session-callable-switch.json"
	$(PYTHON) tests/runtime2/cp19/session_struct_cli.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/session-struct.json"
	$(PYTHON) tests/runtime2/cp19/session_struct_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/session-struct-switch.json"
