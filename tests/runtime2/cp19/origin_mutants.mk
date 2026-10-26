# Focused E4/E8 rerun: only vm_exception.cpp has hooks for these two mutants.
# This supplements, rather than claims, the clean full CP13B mutation campaign.
$(TEST_DIR)/cp19/origin-m%.o: src/rt/vm_exception.cpp $(RT_VALUE_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNIFT_EXC_MUTANT=$* -c $< -o $@
$(TEST_DIR)/cp19/origin-m%$(EXEEXT): $(TEST_DIR)/cp19/origin-m%.o $(filter-out src/rt/vm_exception.o,$(CLI_OBJECTS)) $(LIBFFI_A)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(filter-out src/rt/vm_exception.o,$(CLI_OBJECTS)) $< $(LDLIBS) -o $@
.PHONY: cp19-origin-focus
cp19-origin-focus: $(TARGET) $(TEST_DIR)/cp19/origin-m4$(EXEEXT) $(TEST_DIR)/cp19/origin-m8$(EXEEXT)
	$(PYTHON) tests/runtime2/cp13b/run.py --nift "$(CURDIR)/$(TEST_DIR)/cp19/origin-m4$(EXEEXT)" --oracle "$(CURDIR)/$(TARGET)" --mutant 4
	$(PYTHON) tests/runtime2/cp13b/run.py --nift "$(CURDIR)/$(TEST_DIR)/cp19/origin-m8$(EXEEXT)" --oracle "$(CURDIR)/$(TARGET)" --mutant 8
