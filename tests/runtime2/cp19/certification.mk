# CP19 certification reachability (integrator, 2026-10-29).
#
# Before this file only test-cp19-shared-eval, test-cp19-r1, test-cp19-s1-dispatch(-unit),
# test-cp19-bytes, test-cp19-expression-sanitize and test-cp19-shell-exception reached
# certify-targets; the accepted R2 suites below were run by hand only. Every accepted CP19
# invariant now has a certification path through test-cp19-certify (in
# CERTIFY_RUNTIME2_GATES). The edges are declared one per line in the top-level Makefile
# (after the include of this file); certification_guard.sh requires each one, and its
# self-test removes each one and must see certification fail.
#
# Classification:
#   portable functional/resource-lifetime  test-cp19-functional
#   platform-specific Host/terminal        test-cp19-host (Linux: PTY + LD_PRELOAD fault shim)
#   mutation and allocation                 test-cp19-mutants
#   sanitizers (ASan/LSan/UBSan)            test-cp19-sanitize (+ Linux Host sanitizer runs)
#   comparator sort off the frozen profile  inside test-cp19-session-cli: the documented
#                                           session refusal is asserted exactly and reported
#                                           as UNVERIFIED COMPARATOR COVERAGE;
#                                           NIFT_CP14B_REQUIRE_COMPARATOR=1 (milestone GCC 15.2
#                                           run) makes a refusing binary fail.
#   pending owner decision                  test-cp19-proof-cli needs CP19_LEGACY_ORACLE (the
#                                           frozen legacy binary is not designated); it is
#                                           deliberately NOT in test-cp19-certify and is listed
#                                           as PENDING in the guard output, never skipped silently.
.PHONY: test-cp19-certify test-cp19-functional test-cp19-host test-cp19-mutants test-cp19-sanitize \
	test-cp19-host-sanitize
ifeq ($(shell uname -s),Linux)
# The sanitized job, resource and allocation suites (built by jobs.mk/resources.mk but, until
# now, never run by any target) and the 72-case cleanup matrix on the sanitized CLI. The ASan
# runtime is preloaded ahead of the fault shim (CP19_ASAN_PRELOAD), as the matrix requires.
CP19_SAN_SUITES := jobs jobs-switch resources resources-switch resource-alloc resource-alloc-switch
test-cp19-host-sanitize: $(addprefix $(TEST_DIR)/cp19/,$(addsuffix -sanitize,$(CP19_SAN_SUITES))) \
		$(SAN_TARGET) $(TEST_DIR)/cp19/fail-restore.so
	@set -e; for suite in $(CP19_SAN_SUITES); do \
	  data=$$(mktemp -d); \
	  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 900 \
	    "$(TEST_DIR)/cp19/$$suite-sanitize" "$$data"; \
	  rm -rf "$$data"; \
	done
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	  CP19_ASAN_PRELOAD="$$(readlink -f "$$($(CXX) -print-file-name=libasan.so)")" \
	  $(PYTHON) tests/runtime2/cp19/cleanup_cli.py --nift "$(CURDIR)/$(SAN_TARGET)" \
	    --fault "$(abspath $(TEST_DIR)/cp19/fail-restore.so)" --out "$(TEST_DIR)/cp19/cleanup-cli-sanitize.json"
endif
