# Host builds for testing without the board.
#   make test      all unit, UI and end-to-end tests
#   make sim       build both simulators only
CXX ?= g++
WARN = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror
SAN  = -fsanitize=address,undefined -fno-sanitize-recover=all -g
OLD  = ../SessionLogStation
NEW  = ../ece198Codes1.0.2
CXXFLAGS = -std=c++17 -O1 $(WARN) $(SAN)
# Sketches are compiled with the stub headers. -Wconversion is relaxed for
# sketch code, which follows Arduino style (int everywhere).
SKETCHFLAGS = -std=c++17 -O1 -Wall -Wextra -Wshadow -Werror $(SAN) -Istubs
STUBS = stubs/sim_hw.cpp
STUBH = $(wildcard stubs/*.h)

.PHONY: all sim test unit clean check-copies
all: test_session_log test_profile_store test_ui sim_device sim_ui_device

test_session_log: test_session_log.cpp $(OLD)/session_log.cpp $(OLD)/session_log.h
	$(CXX) $(CXXFLAGS) -I$(OLD) -o $@ test_session_log.cpp $(OLD)/session_log.cpp

test_profile_store: test_profile_store.cpp $(NEW)/profile_store.cpp $(NEW)/profile_store.h
	$(CXX) $(CXXFLAGS) -I$(NEW) -o $@ test_profile_store.cpp $(NEW)/profile_store.cpp

# Original transfer demo sketch.
sim_device: sim_device.cpp $(OLD)/SessionLogStation.ino $(OLD)/session_log.cpp stubs/sim_pty.cpp $(STUBS) $(STUBH)
	$(CXX) $(SKETCHFLAGS) -I$(OLD) -o $@ sim_device.cpp stubs/sim_pty.cpp $(STUBS) $(OLD)/session_log.cpp

# Merged 1.0.2 sketch over a pty, for the end-to-end tests.
sim_ui_device: sim_ui_device.cpp $(NEW)/*.ino $(NEW)/*.cpp $(NEW)/*.h stubs/sim_pty.cpp $(STUBS) $(STUBH)
	$(CXX) $(SKETCHFLAGS) -I$(NEW) -o $@ sim_ui_device.cpp stubs/sim_pty.cpp $(STUBS) \
	  $(NEW)/session_log.cpp $(NEW)/profile_store.cpp

# Merged 1.0.2 sketch in-process with virtual time, for UI tests and fuzzing.
test_ui: test_ui.cpp $(NEW)/*.ino $(NEW)/*.cpp $(NEW)/*.h $(STUBS) $(STUBH)
	$(CXX) $(SKETCHFLAGS) -I$(NEW) -o $@ test_ui.cpp $(STUBS) \
	  $(NEW)/session_log.cpp $(NEW)/profile_store.cpp

sim: sim_device sim_ui_device

# The two sketch folders each need their own copy of session_log (the Arduino
# IDE only compiles files inside the sketch folder). Keep them identical.
check-copies:
	cmp $(OLD)/session_log.h $(NEW)/session_log.h
	cmp $(OLD)/session_log.cpp $(NEW)/session_log.cpp

unit: check-copies test_session_log test_profile_store test_ui
	./test_session_log
	./test_profile_store
	./test_ui

test: unit sim_device sim_ui_device
	./test_end_to_end.bash
	./test_ui_end_to_end.bash

clean:
	rm -f test_session_log test_profile_store test_ui sim_device sim_ui_device
