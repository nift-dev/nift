$(TEST_DIR)/cp19/jobs: tests/runtime2/cp19/jobs.cpp $(CP19_EXPR_OBJECTS) src/shell/Spawn.o
	@mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) src/shell/Spawn.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/jobs-switch: tests/runtime2/cp19/jobs.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o src/shell/Spawn.o
	@mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o src/shell/Spawn.o $(LDLIBS) -o $@
.PHONY: test-cp19-jobs
test-cp19-jobs: $(TEST_DIR)/cp19/jobs $(TEST_DIR)/cp19/jobs-switch
	@mkdir -p $(TEST_DIR)/cp19/job-data
	$(TEST_DIR)/cp19/jobs $(CURDIR)/$(TEST_DIR)/cp19/job-data
	$(TEST_DIR)/cp19/jobs-switch $(CURDIR)/$(TEST_DIR)/cp19/job-data
$(TEST_DIR)/cp19/jobs-sanitize: tests/runtime2/cp19/jobs.cpp $(CP19_EXPR_SAN_OBJECTS) $(TEST_DIR)/san/src/shell/Spawn.o
	@mkdir -p "$(dir $@)"
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt $(SANITIZER_FLAGS) -std=c++17 -pthread $< $(CP19_EXPR_SAN_OBJECTS) $(TEST_DIR)/san/src/shell/Spawn.o $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@
$(TEST_DIR)/cp19/jobs-switch-sanitize: tests/runtime2/cp19/jobs.cpp $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o $(TEST_DIR)/san/src/shell/Spawn.o
	@mkdir -p "$(dir $@)"
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt $(SANITIZER_FLAGS) -std=c++17 -pthread $< $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o $(TEST_DIR)/san/src/shell/Spawn.o $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@
$(TEST_DIR)/cp19/job-control: tests/runtime2/cp19/jobs_mutants.cpp $(CP19_EXPR_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
define CP19_JOB_MUTANT
$(TEST_DIR)/cp19/job-m$(1).o: src/rt/session.cpp
	@mkdir -p "$$(dir $$@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_JOB_MUTANT=$(1) -MMD -MP -c $$< -o $$@
$(TEST_DIR)/cp19/job-m$(1): tests/runtime2/cp19/jobs_mutants.cpp $(filter-out src/rt/session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/job-m$(1).o
	@mkdir -p "$$(dir $$@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $$< $(filter-out src/rt/session.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/job-m$(1).o $(LDLIBS) -o $$@
endef
$(foreach M,1 2,$(eval $(call CP19_JOB_MUTANT,$(M))))
.PHONY: test-cp19-job-mutants
test-cp19-job-mutants: $(TEST_DIR)/cp19/job-control $(TEST_DIR)/cp19/job-m1 $(TEST_DIR)/cp19/job-m2
	$(PYTHON) tests/runtime2/cp19/jobs_mutants.py --dir $(CURDIR)/$(TEST_DIR)/cp19 --out $(TEST_DIR)/cp19/job-mutants.json

.PHONY: test-cp19-job-cli
test-cp19-job-cli: $(TARGET) $(CP13B_SWITCH_NIFT)
	$(PYTHON) tests/runtime2/cp19/jobs_cli.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/jobs-cli-default.json"
	$(PYTHON) tests/runtime2/cp19/jobs_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/jobs-cli-switch.json"
$(TEST_DIR)/cp19/function-owner-oom: tests/rt/value_destroy_oom.cpp $(CP19_EXPR_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/function-owner-oom-sanitize: tests/rt/value_destroy_oom.cpp $(CP19_EXPR_SAN_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt $(SANITIZER_FLAGS) -std=c++17 -pthread $< $(CP19_EXPR_SAN_OBJECTS) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@

ifeq ($(shell uname -s),Linux)
$(TEST_DIR)/cp19/fail-restore.so: tests/runtime2/cp19/fail_restore.c
	@mkdir -p "$(dir $@)"
	$(CC) -shared -fPIC $< -ldl -o "$@"
.PHONY: test-cp19-cleanup-cli
test-cp19-cleanup-cli: $(TARGET) $(CP13B_SWITCH_NIFT) $(TEST_DIR)/cp19/fail-restore.so
	$(PYTHON) tests/runtime2/cp19/cleanup_cli.py --nift "$(CURDIR)/$(TARGET)" --fault "$(abspath $(TEST_DIR)/cp19/fail-restore.so)" --out "$(TEST_DIR)/cp19/cleanup-cli.json"
	$(PYTHON) tests/runtime2/cp19/cleanup_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --fault "$(abspath $(TEST_DIR)/cp19/fail-restore.so)" --out "$(TEST_DIR)/cp19/cleanup-cli-switch.json"
endif
