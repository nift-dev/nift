# CP23 certification reachability (integrator, 2026-10-29, CP19 x CP23 integration).
#
# Before this file no CP23 gate reached certify-targets. test-cp23-certify (in
# CERTIFY_RUNTIME2_GATES) now reaches them; the edges are declared one per line in
# the top-level Makefile (certification_guard_selftest.sh removes each and must see
# certification fail). Classification:
#   Linux local gates (GNU timeout, LD/ptrace-free sanitizers, the GNU mutation
#   linker; macOS/Windows CP23 certification remains a platform obligation)
#     test-runtime2-cp23            aggregate units + 85-row compatibility, both dispatches
#     test-cp23-cancellation        completion races and Error/consumed-cancellation cases,
#                                   1/2/4/8 permits, both dispatches
#     test-cp23-cp19-combined       CP23 x CP19 combined witnesses (resources, jobs, sessions)
#     test-cp23-mutants             six real source mutants with 48 controls
#     test-cp23-allocation          fresh-process allocation containment, awaited/unawaited/
#                                   cancelled/cycle, one-shot and persistent
#     test-runtime2-cp23-sanitize   ASan/LSan/UBSan units
#     test-runtime2-cp23-tsan       TSan units
# Performance (performance.py) is never a certification edge here: it is measured on
# the reference host under the owner's performance plan.
.PHONY: test-cp23-certify test-cp23-cancellation test-cp23-cp19-combined test-cp23-mutants test-cp23-allocation
test-cp23-cancellation: $(TARGET) $(CP13B_SWITCH_NIFT)
	$(PYTHON) tests/runtime2/cp23/cancellation.py --nift "$(CURDIR)/$(TARGET)" --output "$(TEST_DIR)/cp23-cancellation.json"
	$(PYTHON) tests/runtime2/cp23/cancellation.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --output "$(TEST_DIR)/cp23-cancellation-switch.json"
test-cp23-cp19-combined: $(TARGET) $(CP13B_SWITCH_NIFT)
	$(PYTHON) tests/runtime2/cp23/cp19_combined.py --nift "$(CURDIR)/$(TARGET)" --switch "$(CURDIR)/$(CP13B_SWITCH_NIFT)" \
	  --output "$(TEST_DIR)/cp23-cp19-combined.json"
test-cp23-mutants: $(TEST_DIR)/rt/cp23-ownership-unit $(TEST_DIR)/rt/cp23-parallel-unit
	$(PYTHON) tests/runtime2/cp23/mutations.py --evidence "$(TEST_DIR)/cp23-mutants-evidence"
test-cp23-allocation: $(TEST_DIR)/rt/cp23-allocation-unit
	@set -e; for mode in awaited unawaited cancelled cycle; do for p in "" --persistent; do \
	  $(PYTHON) tests/runtime2/cp23/allocation.py --binary "$(TEST_DIR)/rt/cp23-allocation-unit" --mode $$mode $$p \
	    --output "$(TEST_DIR)/cp23-allocation-$$mode$$p.json"; done; done
