# Host builds for testing without the board.
#   make test      every test: firmware simulators, protocol, laptop database
#   make sim       build the simulators only
CXX ?= g++
WARN = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror
SAN  = -fsanitize=address,undefined -fno-sanitize-recover=all -g
OLD  = ../SessionLogStation
CXXFLAGS = -std=c++17 -O1 $(WARN) $(SAN)
# Sketches are compiled with the stub headers. -Wconversion is relaxed for
# sketch code (Arduino style). -Wno-format-truncation: on the host `long` is
# 64-bit, so GCC sizes %lu for 20 digits; on the R4 it is 32-bit and fits.
SKETCHFLAGS = -std=c++17 -O1 -Wall -Wextra -Wshadow -Werror -Wno-format-truncation $(SAN) -Istubs
STUBS = stubs/sim_hw.cpp stubs/sim_pty.cpp
STUBH = $(wildcard stubs/*.h)

.PHONY: all sim test clean
all: test_session_log sim_device sim_dosage_device sim_dosage_device_107

test_session_log: test_session_log.cpp $(OLD)/session_log.cpp $(OLD)/session_log.h
	$(CXX) $(CXXFLAGS) -I$(OLD) -o $@ test_session_log.cpp $(OLD)/session_log.cpp

# Original SessionLogStation transfer demo.
sim_device: sim_device.cpp $(OLD)/SessionLogStation.ino $(OLD)/session_log.cpp $(STUBS) $(STUBH)
	$(CXX) $(SKETCHFLAGS) -I$(OLD) -o $@ sim_device.cpp $(STUBS) $(OLD)/session_log.cpp

# Dosage firmware 1.0.8 (current) and 1.0.7 (to test that it is refused).
sim_dosage_device: sim_dosage_device.cpp ../ece198Codes108/ece198Codes108.ino $(STUBS) $(STUBH)
	$(CXX) $(SKETCHFLAGS) -I../ece198Codes108 -DHAS_IDENTITY -o $@ sim_dosage_device.cpp $(STUBS)

sim_dosage_device_107: sim_dosage_device.cpp ../ece198Codes1.0.7.ino $(STUBS) $(STUBH)
	$(CXX) $(SKETCHFLAGS) -I.. -DSKETCH='"ece198Codes1.0.7.ino"' -o $@ sim_dosage_device.cpp $(STUBS)

sim: sim_device sim_dosage_device sim_dosage_device_107

test: all
	./test_session_log
	./test_end_to_end.bash
	cd ../laptop && python3 test_dosage_db.py
	python3 test_dosage_end_to_end.py

clean:
	rm -f test_session_log sim_device sim_dosage_device sim_dosage_device_107
