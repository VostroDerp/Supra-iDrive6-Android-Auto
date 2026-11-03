# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#
# Host tests on Linux (no QNX SDK). From milestone-7/:
#   make -C tests -j8          build and run every test; only what changed since the last pass reruns
#   make -C tests build        build only
#   make -C tests list         tests and the src/ files each one links
#   make -C tests clean
# Every test_*.cpp is picked up on its own; a test that links code from src/ names it in SRC_<test> below.
# -std=gnu++98 is what the car's GCC 4.4.2 (QNX 6.5) accepts, so a test can't pass on code the car can't build.
# test_session_lifecycle also runs under ThreadSanitizer when the compiler has it (libtsan); otherwise skipped.
# Tests run from milestone-7/ (their fixture paths are relative to it). Logs: build/<test>.log.

M7    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
T     := $(M7)/tests
OUT   ?= $(T)/build
CXX   ?= g++
PYTHON ?= python3
TIMEOUT ?= 120

STD      := -std=gnu++98
CPPFLAGS := -DNBT_HOST_TEST -I$(M7)/include -I$(M7)/src -I$(T)
CXXFLAGS ?= -O1 -g
WARN     := -Wall -Wextra
LDLIBS   := -pthread -ldl

TESTS := $(sort $(patsubst $(T)/%.cpp,%,$(wildcard $(T)/test_*.cpp)))
PYTESTS := $(sort $(patsubst $(T)/%.py,%,$(wildcard $(T)/test_*.py)))

# ---- src/ files each test links (found by linking every test alone; add yours here) -------------------------
SRC_test_ap_candidates     := wireless_setup
SRC_test_audio_mixer       := audio_mixer
SRC_test_audio_route       := audio_route
SRC_test_bcl_detect        := wireless_setup
SRC_test_bt_play           := bt_play
SRC_test_car_config        := sdr_builder
SRC_test_kombi_nav         := kombi_client kombi_nav
SRC_test_pcm               := pcm_utils
SRC_test_sdr               := sdr_builder
SRC_test_sdr_drop          := sdr_builder
SRC_test_session_lifecycle := aa_session sdr_builder crash_handler
SRC_test_touch             := touch_probe
SRC_test_usb_accessory     := usb_accessory usb_direct
SRC_test_voice_scenario    := voice_scenario
SRC_test_wpp               := wireless_setup

# ---- per-test extras -----------------------------------------------------------------------------------------
# The lifecycle test swaps OpenSSL for a stub cryptor; its src/ objects are built with the stub too (obj-stub/).
STUB_test_session_lifecycle := -include $(T)/stub_cryptor.hpp
# Touch replay only: no car peer file in /var/opt/aa, no connected-UDP routing. Also run with split datagrams.
ENV_test_touch := NBT_TOUCH_CONNECT=0
# Writes t_iap2_wifi.cfg in its working directory: run it in build/.
DIR_test_wpp := $(OUT)

OBJDIR = $(if $(STUB_$(1)),obj-stub,obj)
TEST_BIN = $(OUT)/bin/$(1)
TEST_OBJS = $(OUT)/tobj/$(1).o $(foreach s,$(SRC_$(1)),$(OUT)/$(call OBJDIR,$(1))/$(s).o)

TSAN_OK := $(shell echo 'int main(){return 0;}' | $(CXX) -fsanitize=thread -x c++ - -o /dev/null 2>/dev/null && echo 1)

STAMPS := $(foreach t,$(TESTS),$(OUT)/ok/$(t)) $(OUT)/ok/test_touch_split $(OUT)/ok/rhmiapp_build \
          $(foreach p,$(PYTESTS),$(OUT)/ok/$(p)) $(if $(TSAN_OK),$(OUT)/ok/test_session_lifecycle_tsan)

.PHONY: all check build list clean
.DELETE_ON_ERROR:

all: check

check: $(STAMPS)
	@echo "host tests: $(words $(STAMPS)) passed$(if $(TSAN_OK),, (ThreadSanitizer run skipped: no libtsan))"

build: $(foreach t,$(TESTS),$(call TEST_BIN,$(t))) $(OUT)/libNbtAad.so

list:
	@$(foreach t,$(TESTS),printf '%-24s %s\n' $(t) '$(or $(SRC_$(t)),-)';)
	@$(foreach p,$(PYTESTS),printf '%-24s %s\n' $(p).py '(python)';)

clean:
	rm -rf $(OUT)

# ---- compile -------------------------------------------------------------------------------------------------
$(OUT)/obj/%.o: $(M7)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(STD) $(CPPFLAGS) $(CXXFLAGS) $(WARN) -MMD -MP -c $< -o $@

$(OUT)/obj-stub/%.o: $(M7)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(STD) $(CPPFLAGS) $(CXXFLAGS) $(WARN) -include $(T)/stub_cryptor.hpp -MMD -MP -c $< -o $@

$(OUT)/tobj/%.o: $(T)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(STD) $(CPPFLAGS) $(CXXFLAGS) $(WARN) $(STUB_$*) -MMD -MP -c $< -o $@

# ---- link and run (one rule set per test) --------------------------------------------------------------------
define TEST_rules
$(call TEST_BIN,$(1)): $(call TEST_OBJS,$(1))
	@mkdir -p $$(@D)
	$$(CXX) $$^ -o $$@ $$(LDLIBS)

$(OUT)/ok/$(1): $(call TEST_BIN,$(1))
	@mkdir -p $$(@D)
	@echo "[TEST] $(1)"
	@cd $(or $(DIR_$(1)),$(M7)) && env $(ENV_$(1)) timeout $(TIMEOUT) $$< > $(OUT)/$(1).log 2>&1 \
	    || { echo "[FAIL] $(1) (exit $$$$?), last lines of $(OUT)/$(1).log:"; tail -20 $(OUT)/$(1).log; exit 1; }
	@touch $$@
endef
$(foreach t,$(TESTS),$(eval $(call TEST_rules,$(t))))

# After the plain run, never alongside it: both bind the touch forwarder's UDP port.
$(OUT)/ok/test_touch_split: $(call TEST_BIN,test_touch) | $(OUT)/ok/test_touch
	@mkdir -p $(@D)
	@echo "[TEST] test_touch (split datagrams)"
	@cd $(M7) && env $(ENV_test_touch) TOUCH_SPLIT=1 timeout $(TIMEOUT) $< > $(OUT)/test_touch_split.log 2>&1 \
	    || { echo "[FAIL] test_touch split, last lines of $(OUT)/test_touch_split.log:"; tail -20 $(OUT)/test_touch_split.log; exit 1; }
	@touch $@

# ThreadSanitizer build of the lifecycle test (own objects: -fsanitize=thread must be on every unit).
TSAN_FLAGS := -fsanitize=thread -O1 -g
$(OUT)/tsan/test_session_lifecycle: $(T)/test_session_lifecycle.cpp $(foreach s,$(SRC_test_session_lifecycle),$(M7)/src/$(s).cpp)
	@mkdir -p $(@D)
	$(CXX) $(STD) $(CPPFLAGS) $(TSAN_FLAGS) $(STUB_test_session_lifecycle) -MMD -MP -MF $@.d $^ -o $@ $(LDLIBS)

$(OUT)/ok/test_session_lifecycle_tsan: $(OUT)/tsan/test_session_lifecycle
	@mkdir -p $(@D)
	@echo "[TEST] test_session_lifecycle (ThreadSanitizer)"
	@cd $(M7) && TSAN_OPTIONS=halt_on_error=1 timeout $(TIMEOUT) $< > $(OUT)/test_session_lifecycle_tsan.log 2>&1 \
	    || { echo "[FAIL] test_session_lifecycle under TSAN, last lines of $(OUT)/test_session_lifecycle_tsan.log:"; \
	         tail -30 $(OUT)/test_session_lifecycle_tsan.log; exit 1; }
	@touch $@

# The iDrive app compiles as a host shared library (catches C++ errors without the QNX SDK). The old
# rhmiapp_host_test harness is not run: its fake framework predates the current bmw::PluginApp ABI.
$(OUT)/libNbtAad.so: $(M7)/rhmiapp/nbt_aad_app.cpp
	@mkdir -p $(@D)
	$(CXX) $(STD) $(CPPFLAGS) $(CXXFLAGS) $(WARN) -fPIC -shared -MMD -MP -MF $@.d $< -o $@

$(OUT)/ok/rhmiapp_build: $(OUT)/libNbtAad.so
	@mkdir -p $(@D)
	@touch $@

# Python tests compile code out of src/ themselves: rerun when src/ changes.
$(OUT)/ok/test_%: $(T)/test_%.py $(wildcard $(M7)/src/*.cpp $(M7)/src/*.hpp)
	@mkdir -p $(@D)
	@echo "[TEST] test_$*.py"
	@cd $(M7) && timeout $(TIMEOUT) $(PYTHON) $< > $(OUT)/test_$*.log 2>&1 \
	    || { echo "[FAIL] test_$*.py, last lines of $(OUT)/test_$*.log:"; tail -20 $(OUT)/test_$*.log; exit 1; }
	@touch $@

-include $(shell find $(OUT) -name '*.d' 2>/dev/null)
