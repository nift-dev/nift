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
.PHONY: test-cp19-admission-mutants
test-cp19-admission-mutants: $(TEST_DIR)/cp19/admission-control $(addprefix $(TEST_DIR)/cp19/admission-m,1 2 3 4)
	$(PYTHON) tests/runtime2/cp19/admission_mutants.py --dir "$(CURDIR)/$(TEST_DIR)/cp19" --out "$(TEST_DIR)/cp19/admission-mutants.json"
