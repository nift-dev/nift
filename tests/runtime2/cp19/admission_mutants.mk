# Restores each corrected over-refusal in a real production translation unit.
$(TEST_DIR)/cp19/admission-control: tests/runtime2/cp19/admission_mutants.cpp $(CP19_EXPR_OBJECTS)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
define CP19_ADMISSION_MUTANT
$(TEST_DIR)/cp19/admission-m$(1).o: src/rt/$(2).cpp $(RT_VALUE_HEADERS)
	$$(CXX) $$(CPPFLAGS) $$(CXXFLAGS) -DNIFT_SESSION_ADMISSION_MUTANT=$(1) -c $$< -o $$@
$(TEST_DIR)/cp19/admission-m$(1): tests/runtime2/cp19/admission_mutants.cpp $(filter-out src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/admission-m$(1).o
	$$(CXX) $$(CPPFLAGS) -Itests/rt $$(CXXFLAGS) $$< $(filter-out src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/admission-m$(1).o $$(LDLIBS) -o $$@
endef
$(eval $(call CP19_ADMISSION_MUTANT,1,compiler_session))
$(eval $(call CP19_ADMISSION_MUTANT,2,compiler_session_scope))
$(eval $(call CP19_ADMISSION_MUTANT,3,cell))
$(eval $(call CP19_ADMISSION_MUTANT,4,compiler_session))
$(eval $(call CP19_ADMISSION_MUTANT,5,compiler_session))
$(eval $(call CP19_ADMISSION_MUTANT,6,compiler_session))
$(eval $(call CP19_ADMISSION_MUTANT,7,compiler_session_contract))
$(eval $(call CP19_ADMISSION_MUTANT,8,compiler_session_contract))
$(eval $(call CP19_ADMISSION_MUTANT,9,compiler_expr))
$(eval $(call CP19_ADMISSION_MUTANT,10,compiler_session_native))
$(eval $(call CP19_ADMISSION_MUTANT,11,compiler_session_native))
$(eval $(call CP19_ADMISSION_MUTANT,12,session_callable))
$(eval $(call CP19_ADMISSION_MUTANT,13,infer_session_location))
.PHONY: test-cp19-admission-mutants
test-cp19-admission-mutants: $(TEST_DIR)/cp19/admission-control $(addprefix $(TEST_DIR)/cp19/admission-m,1 2 3 4 5 6 7 8 9 10 11 12 13)
	$(PYTHON) tests/runtime2/cp19/admission_mutants.py --dir "$(CURDIR)/$(TEST_DIR)/cp19" --out "$(TEST_DIR)/cp19/admission-mutants.json"

$(TEST_DIR)/cp19/admission-sanitize: tests/runtime2/cp19/admission_mutants.cpp $(CP19_EXPR_SAN_OBJECTS)
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt $(SANITIZER_FLAGS) -std=c++17 -pthread $< $(CP19_EXPR_SAN_OBJECTS) $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@
$(TEST_DIR)/cp19/admission-switch-sanitize: tests/runtime2/cp19/admission_mutants.cpp $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o
	$(CXX) $(SAN_CPPFLAGS) -Itests/rt $(SANITIZER_FLAGS) -std=c++17 -pthread $< $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o $(filter-out $(LIBFFI_A),$(LDLIBS)) -o $@
.PHONY: test-cp19-admission-sanitize
test-cp19-admission-sanitize: $(TEST_DIR)/cp19/admission-sanitize $(TEST_DIR)/cp19/admission-switch-sanitize
	@for binary in $^; do for case in wrapper field loop alias schema order direct nested capture numeric native native-once selector shared; do ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $$binary $$case admitted || exit $$?; done; done

$(TEST_DIR)/cp19/admission-control-switch: tests/runtime2/cp19/admission_mutants.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
define CP19_ADMISSION_SWITCH
$(TEST_DIR)/cp19/admission-m$(1)-switch: tests/runtime2/cp19/admission_mutants.cpp $(filter-out src/rt/vm.o src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/admission-m$(1).o $(TEST_DIR)/cp13b/vm-switch.o
	$$(CXX) $$(CPPFLAGS) -Itests/rt $$(CXXFLAGS) $$< $(filter-out src/rt/vm.o src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/admission-m$(1).o $(TEST_DIR)/cp13b/vm-switch.o $$(LDLIBS) -o $$@
endef
$(eval $(call CP19_ADMISSION_SWITCH,1,compiler_session))
$(eval $(call CP19_ADMISSION_SWITCH,2,compiler_session_scope))
$(eval $(call CP19_ADMISSION_SWITCH,3,cell))
$(eval $(call CP19_ADMISSION_SWITCH,4,compiler_session))
$(eval $(call CP19_ADMISSION_SWITCH,5,compiler_session))
$(eval $(call CP19_ADMISSION_SWITCH,6,compiler_session))
$(eval $(call CP19_ADMISSION_SWITCH,7,compiler_session_contract))
$(eval $(call CP19_ADMISSION_SWITCH,8,compiler_session_contract))
$(eval $(call CP19_ADMISSION_SWITCH,9,compiler_expr))
$(eval $(call CP19_ADMISSION_SWITCH,10,compiler_session_native))
$(eval $(call CP19_ADMISSION_SWITCH,11,compiler_session_native))
$(eval $(call CP19_ADMISSION_SWITCH,12,session_callable))
$(eval $(call CP19_ADMISSION_SWITCH,13,infer_session_location))
.PHONY: test-cp19-admission-mutants-switch
test-cp19-admission-mutants-switch: $(TEST_DIR)/cp19/admission-control-switch $(addsuffix -switch,$(addprefix $(TEST_DIR)/cp19/admission-m,1 2 3 4 5 6 7 8 9 10 11 12 13))
	$(PYTHON) tests/runtime2/cp19/admission_mutants.py --dir "$(CURDIR)/$(TEST_DIR)/cp19" --suffix=-switch --out "$(TEST_DIR)/cp19/admission-mutants-switch.json"
$(TEST_DIR)/cp19/session-control-switch: tests/runtime2/cp19/session_mutants.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
define CP19_SESSION_SWITCH
$(TEST_DIR)/cp19/session-m$(1)-switch: tests/runtime2/cp19/session_mutants.cpp $(filter-out src/rt/vm.o src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m$(1).o $(TEST_DIR)/cp13b/vm-switch.o
	$$(CXX) $$(CPPFLAGS) -Itests/rt $$(CXXFLAGS) $$< $(filter-out src/rt/vm.o src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/session-m$(1).o $(TEST_DIR)/cp13b/vm-switch.o $$(LDLIBS) -o $$@
endef
$(eval $(call CP19_SESSION_SWITCH,1,session))
$(eval $(call CP19_SESSION_SWITCH,2,session))
$(eval $(call CP19_SESSION_SWITCH,3,vm_session))
$(eval $(call CP19_SESSION_SWITCH,4,session))
$(eval $(call CP19_SESSION_SWITCH,5,vm_closure))
$(eval $(call CP19_SESSION_SWITCH,6,compiler_assign_expr))
$(eval $(call CP19_SESSION_SWITCH,7,compiler_session_contract))
.PHONY: test-cp19-session-mutants-switch
test-cp19-session-mutants-switch: $(TEST_DIR)/cp19/session-control-switch $(addsuffix -switch,$(addprefix $(TEST_DIR)/cp19/session-m,1 2 3 4 5 6 7))
	$(PYTHON) tests/runtime2/cp19/session_mutants.py --dir "$(CURDIR)/$(TEST_DIR)/cp19" --suffix=-switch --out "$(TEST_DIR)/cp19/session-mutants-switch.json"
