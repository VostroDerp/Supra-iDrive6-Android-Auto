# Desk rig: NBT-AAD's Android Auto session (unmodified) against a real phone over USB, on Linux.
# Build against any milestone-7 tree: make NBT_SRC=<path to milestone-7>. Default: the -usb worktree (PR #42 work).
# Needs libusb-1.0 and OpenSSL development headers. Outputs go to build/ (gitignored).

NBT_SRC ?= $(abspath ../../../../Supra-iDrive6-Android-Auto-usb/milestone-7)
BUILD   := build
CXX     ?= g++
# -w on the daemon's sources only: they are written for GCC 4.4 / OpenSSL 0.9.8 and warn on a modern host.
CXXFLAGS ?= -O1 -g
CXXFLAGS += -std=gnu++11 -pthread -I$(NBT_SRC)/include -I$(NBT_SRC)/src
LIBS     := -lusb-1.0 -lssl -lcrypto -lpthread

NBT_SRCS := aa_session aa_cryptor sdr_builder crash_handler
NBT_OBJS := $(addprefix $(BUILD)/nbt/,$(addsuffix .o,$(NBT_SRCS)))

.PHONY: all clean run list check-src

all: $(BUILD)/desk_rig

check-src:
	@test -f $(NBT_SRC)/src/aa_session.cpp || { echo "NBT_SRC=$(NBT_SRC) is not a milestone-7 tree"; exit 1; }

$(BUILD)/nbt/%.o: $(NBT_SRC)/src/%.cpp | check-src
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -w -c $< -o $@

$(BUILD)/desk_rig.o: desk_rig.cpp | check-src
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -Wall -Wextra -c $< -o $@

$(BUILD)/desk_rig: $(BUILD)/desk_rig.o $(NBT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "built $@ from $(NBT_SRC) ($$(git -C $(NBT_SRC) describe --always --dirty 2>/dev/null))"

# One session, logged, then the same stage table car.sh prints for a car run.
run: $(BUILD)/desk_rig
	@mkdir -p $(BUILD)/out
	$(BUILD)/desk_rig --out $(BUILD)/out $(ARGS) 2>&1 | tee $(BUILD)/out/session.log; \
	../car/car.sh stages $(BUILD)/out/session.log

list: $(BUILD)/desk_rig
	$(BUILD)/desk_rig --list

clean:
	rm -rf $(BUILD)
