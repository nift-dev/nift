$(TEST_DIR)/cp19/resources: tests/runtime2/cp19/resources.cpp $(CP19_EXPR_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/resources-switch: tests/runtime2/cp19/resources.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
.PHONY: test-cp19-resources
test-cp19-resources: $(TEST_DIR)/cp19/resources $(TEST_DIR)/cp19/resources-switch
	@set -e; for binary in $^; do folder=$$(mktemp -d); $$binary "$$folder"; rm -r "$$folder"; done

$(TEST_DIR)/cp19/resource-control: tests/runtime2/cp19/resource_mutants.cpp $(CP19_EXPR_OBJECTS)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/resource-control-switch: tests/runtime2/cp19/resource_mutants.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
define CP19_RESOURCE_MUTANT
$(TEST_DIR)/cp19/resource-m$(1).o: src/rt/$(2).cpp $(RT_VALUE_HEADERS)
	$$(CXX) $$(CPPFLAGS) $$(CXXFLAGS) -DNIFT_SESSION_RESOURCE_MUTANT=$(1) -c $$< -o $$@
$(TEST_DIR)/cp19/resource-m$(1): tests/runtime2/cp19/resource_mutants.cpp $(filter-out src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/resource-m$(1).o
	$$(CXX) $$(CPPFLAGS) -Itests/rt $$(CXXFLAGS) $$< $(filter-out src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/resource-m$(1).o $$(LDLIBS) -o $$@
$(TEST_DIR)/cp19/resource-m$(1)-switch: tests/runtime2/cp19/resource_mutants.cpp $(filter-out src/rt/vm.o src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/resource-m$(1).o $(TEST_DIR)/cp13b/vm-switch.o
	$$(CXX) $$(CPPFLAGS) -Itests/rt $$(CXXFLAGS) $$< $(filter-out src/rt/vm.o src/rt/$(2).o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp19/resource-m$(1).o $(TEST_DIR)/cp13b/vm-switch.o $$(LDLIBS) -o $$@
endef
$(eval $(call CP19_RESOURCE_MUTANT,1,vm_handles))
$(eval $(call CP19_RESOURCE_MUTANT,2,vm_session))
$(eval $(call CP19_RESOURCE_MUTANT,3,handles/handle_box))
$(eval $(call CP19_RESOURCE_MUTANT,4,handles/table_contexts))
$(eval $(call CP19_RESOURCE_MUTANT,5,compiler_handles))
$(eval $(call CP19_RESOURCE_MUTANT,6,session_callable))
.PHONY: test-cp19-resource-mutants
test-cp19-resource-mutants: $(TEST_DIR)/cp19/resource-control $(TEST_DIR)/cp19/resource-control-switch $(addprefix $(TEST_DIR)/cp19/resource-m,1 2 3 4 5 6) $(addsuffix -switch,$(addprefix $(TEST_DIR)/cp19/resource-m,1 2 3 4 5 6))
	$(PYTHON) tests/runtime2/cp19/resource_mutants.py --dir "$(CURDIR)/$(TEST_DIR)/cp19" --out "$(TEST_DIR)/cp19/resource-mutants.json"

$(eval $(call CP19_PROOF_SANITIZER,resources,resources))
$(eval $(call CP19_PROOF_SANITIZER,resource-alloc,resource_alloc))
$(TEST_DIR)/cp19/resource-alloc: tests/runtime2/cp19/resource_alloc.cpp $(CP19_EXPR_OBJECTS)
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(CP19_EXPR_OBJECTS) $(LDLIBS) -o $@
$(TEST_DIR)/cp19/resource-alloc-switch: tests/runtime2/cp19/resource_alloc.cpp $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o
	$(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) $< $(filter-out src/rt/vm.o,$(CP19_EXPR_OBJECTS)) $(TEST_DIR)/cp13b/vm-switch.o $(LDLIBS) -o $@
.PHONY: test-cp19-resource-alloc
test-cp19-resource-alloc: $(TEST_DIR)/cp19/resource-alloc $(TEST_DIR)/cp19/resource-alloc-switch
	@set -e; for binary in $^; do folder=$$(mktemp -d); $$binary "$$folder"; rm -r "$$folder"; done

.PHONY: test-cp19-resource-cli
test-cp19-resource-cli: $(TARGET) $(CP13B_SWITCH_NIFT)
	$(PYTHON) tests/runtime2/cp19/resource_cli.py --nift "$(CURDIR)/$(TARGET)" --out "$(TEST_DIR)/cp19/resource-cli-default.json"
	$(PYTHON) tests/runtime2/cp19/resource_cli.py --nift "$(CURDIR)/$(CP13B_SWITCH_NIFT)" --out "$(TEST_DIR)/cp19/resource-cli-switch.json"
