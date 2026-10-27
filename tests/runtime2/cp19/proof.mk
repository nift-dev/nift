# Full CP19 summary proof, source ownership, mutation and allocation controls.
$(TEST_DIR)/cp19/proof: tests/runtime2/cp19/proof.cpp $(CP19_EXPR_OBJECTS)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/proof-switch: tests/runtime2/cp19/proof.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/proof-alloc: tests/runtime2/cp19/proof_alloc.cpp $(CP19_EXPR_OBJECTS)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/proof-alloc-switch: tests/runtime2/cp19/proof_alloc.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
.PHONY: test-cp19-proof test-cp19-proof-alloc
test-cp19-proof: $(TEST_DIR)/cp19/proof $(TEST_DIR)/cp19/proof-switch
	@for binary in $^; do $$binary history admitted || exit $$?; for case in unknown-effect partial-target active active-method active-constructor active-collection active-indirect unknown field element return alias cell unknown-result; do $$binary $$case refused || exit $$?; done; $$binary reporting-arguments prefixed || exit $$?; $$binary windows clean || exit $$?; $$binary array clean || exit $$?; $$binary shared-callable guarded || exit $$?; $$binary reporting prefixed || exit $$?; $$binary identity clean || exit $$?; $$binary lifetime clean || exit $$?; done
test-cp19-proof-alloc: $(TEST_DIR)/cp19/proof-alloc $(TEST_DIR)/cp19/proof-alloc-switch
	$(TEST_DIR)/cp19/proof-alloc
	$(TEST_DIR)/cp19/proof-alloc-switch

define CP19_PROOF_MUTANT
$(TEST_DIR)/cp19/proof-m$(1).o: src/rt/$(2).cpp $(RT_VALUE_HEADERS)
	$$(CXX) $$(CPPFLAGS) $$(CXXFLAGS) -DNIFT_SESSION_ADMISSION_MUTANT=$(1) -c $$< -o $$@
$(TEST_DIR)/cp19/proof-m$(1): tests/runtime2/cp19/proof.cpp $(filter-out src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/proof-m$(1).o
	$$(CXX) $$(CPPFLAGS) -Itests/rt $$(CXXFLAGS) $$< $(filter-out src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/proof-m$(1).o $$(LDLIBS) -o $$@
$(TEST_DIR)/cp19/proof-m$(1)-switch: tests/runtime2/cp19/proof.cpp $(filter-out src/rt/vm.o src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/proof-m$(1).o $(TEST_DIR)/cp13b/vm-switch.o
	$$(CXX) $$(CPPFLAGS) -Itests/rt $$(CXXFLAGS) $$< $(filter-out src/rt/vm.o src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/proof-m$(1).o $(TEST_DIR)/cp13b/vm-switch.o $$(LDLIBS) -o $$@
endef
$(eval $(call CP19_PROOF_MUTANT,14,session_history))
$(eval $(call CP19_PROOF_MUTANT,15,session_history))
$(eval $(call CP19_PROOF_MUTANT,16,session_shape))
$(eval $(call CP19_PROOF_MUTANT,18,session_shape))
$(eval $(call CP19_PROOF_MUTANT,19,session_history))
$(eval $(call CP19_PROOF_MUTANT,20,session_history))
$(eval $(call CP19_PROOF_MUTANT,21,vm_report))
# M17 disables both independent guards in separate production translation units.
$(TEST_DIR)/cp19/proof-m17-shape.o: src/rt/session_shape.cpp $(RT_VALUE_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_ADMISSION_MUTANT=17 -c $< -o $@
$(TEST_DIR)/cp19/proof-m17-history.o: src/rt/session_history.cpp $(RT_VALUE_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_SESSION_ADMISSION_MUTANT=17 -c $< -o $@
$(TEST_DIR)/cp19/proof-m17: tests/runtime2/cp19/proof.cpp $(filter-out src/rt/session_shape.o src/rt/session_history.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/proof-m17-shape.o $(TEST_DIR)/cp19/proof-m17-history.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/session_shape.o src/rt/session_history.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/proof-m17-shape.o $(TEST_DIR)/cp19/proof-m17-history.o $(LDLIBS) -o $@
$(TEST_DIR)/cp19/proof-m17-switch: tests/runtime2/cp19/proof.cpp $(filter-out src/rt/vm.o src/rt/session_shape.o src/rt/session_history.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/proof-m17-shape.o $(TEST_DIR)/cp19/proof-m17-history.o $(TEST_DIR)/cp13b/vm-switch.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o src/rt/session_shape.o src/rt/session_history.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/proof-m17-shape.o $(TEST_DIR)/cp19/proof-m17-history.o $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
.PHONY: test-cp19-proof-mutants
test-cp19-proof-mutants: $(TEST_DIR)/cp19/proof $(TEST_DIR)/cp19/proof-switch $(addprefix $(TEST_DIR)/cp19/proof-m,14 15 16 17 18 19 20 21) $(addsuffix -switch,$(addprefix $(TEST_DIR)/cp19/proof-m,14 15 16 17 18 19 20 21))
	$(PYTHON) tests/runtime2/cp19/proof_mutants.py --dir "$(CURDIR)/$(TEST_DIR)/cp19" --out "$(TEST_DIR)/cp19/proof-mutants.json"
	$(PYTHON) tests/runtime2/cp19/proof_mutants.py --dir "$(CURDIR)/$(TEST_DIR)/cp19" --suffix=-switch --out "$(TEST_DIR)/cp19/proof-mutants-switch.json"
$(eval $(call CP19_PROOF_MUTANT,13,infer_session_location))
$(eval $(call CP19_PROOF_MUTANT,22,compiler_expr))
test-cp19-proof-mutants: $(addprefix $(TEST_DIR)/cp19/proof-m,13 22) $(addsuffix -switch,$(addprefix $(TEST_DIR)/cp19/proof-m,13 22))
$(eval $(call CP19_PROOF_MUTANT,23,compiler_window))
test-cp19-proof-mutants: $(TEST_DIR)/cp19/proof-m23 $(TEST_DIR)/cp19/proof-m23-switch

define CP19_PROOF_SANITIZER
$(TEST_DIR)/cp19/$(1)-sanitize: tests/runtime2/cp19/$(2).cpp $(CP19_EXPR_SAN_OBJECTS)
	$$(CXX) $$(SAN_CPPFLAGS) -Itests/rt $$(SANITIZER_FLAGS) -std=c++17 -pthread $$< $$(CP19_EXPR_SAN_OBJECTS) $$(filter-out $$(LIBFFI_A),$$(LDLIBS)) -o $$@
$(TEST_DIR)/cp19/$(1)-switch-sanitize: tests/runtime2/cp19/$(2).cpp $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o
	$$(CXX) $$(SAN_CPPFLAGS) -Itests/rt $$(SANITIZER_FLAGS) -std=c++17 -pthread $$< $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(CP19_EXPR_SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o $$(filter-out $$(LIBFFI_A),$$(LDLIBS)) -o $$@
endef
$(eval $(call CP19_PROOF_SANITIZER,proof,proof))
$(eval $(call CP19_PROOF_SANITIZER,proof-alloc,proof_alloc))
.PHONY: test-cp19-proof-sanitize
test-cp19-proof-sanitize: $(TEST_DIR)/cp19/proof-sanitize $(TEST_DIR)/cp19/proof-switch-sanitize $(TEST_DIR)/cp19/proof-alloc-sanitize $(TEST_DIR)/cp19/proof-alloc-switch-sanitize
	$(PYTHON) tests/runtime2/cp19/proof_sanitize.py --dir "$(CURDIR)/$(TEST_DIR)/cp19" --out "$(TEST_DIR)/cp19/proof-sanitize.json"
$(TEST_DIR)/cp19/proof-cost: tests/runtime2/cp19/proof_cost.cpp $(CP19_EXPR_OBJECTS)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
.PHONY: test-cp19-proof-cli
test-cp19-proof-cli: $(TARGET) $(CP13B_SWITCH_NIFT)
	@test -n "$(CP19_LEGACY_ORACLE)" || { echo 'Set CP19_LEGACY_ORACLE to the frozen legacy binary'; exit 2; }
	$(PYTHON) tests/runtime2/cp19/session_proof_cli.py --nift "$(CURDIR)/$(TARGET)" --legacy "$(CP19_LEGACY_ORACLE)" --out "$(TEST_DIR)/cp19/proof-cli.json"
	$(PYTHON) tests/runtime2/cp19/session_proof_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --legacy "$(CP19_LEGACY_ORACLE)" --out "$(TEST_DIR)/cp19/proof-cli-switch.json"

$(eval $(call CP19_PROOF_MUTANT,24,compiler_call))
test-cp19-proof-mutants: $(TEST_DIR)/cp19/proof-m24 $(TEST_DIR)/cp19/proof-m24-switch

$(TEST_DIR)/cp19/proof-cli-switch-sanitize: $(filter-out $(TEST_DIR)/san/src/rt/vm.o,$(SAN_OBJECTS)) $(TEST_DIR)/cp19/vm-switch-sanitize.o $(SAN_LIBFFI_A)
	$(CXX) -std=c++17 -pthread $(SANITIZER_FLAGS) $^ -o $@

$(eval $(call CP19_PROOF_MUTANT,25,infer_session))
$(eval $(call CP19_PROOF_MUTANT,26,infer_session_callable))
test-cp19-proof-mutants: $(addprefix $(TEST_DIR)/cp19/proof-m,25 26) $(addsuffix -switch,$(addprefix $(TEST_DIR)/cp19/proof-m,25 26))
