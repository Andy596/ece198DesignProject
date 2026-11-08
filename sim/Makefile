# Host builds for testing without the board.
#   make test      unit tests + end-to-end sync/export tests
#   make sim       build the simulator only
CXX ?= g++
WARN = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror
SAN  = -fsanitize=address,undefined -fno-sanitize-recover=all -g
FW   = ../SessionLogStation
CXXFLAGS = -std=c++17 -O1 $(WARN) $(SAN) -I$(FW)

.PHONY: all sim test clean
all: test_session_log sim_device

test_session_log: test_session_log.cpp $(FW)/session_log.cpp $(FW)/session_log.h
	$(CXX) $(CXXFLAGS) -o $@ test_session_log.cpp $(FW)/session_log.cpp

# The sketch is compiled with the stub headers; sim_device.cpp provides the
# stub implementations. -Wconversion is relaxed only for the sim glue.
sim_device: sim_device.cpp $(FW)/SessionLogStation.ino $(FW)/session_log.cpp stubs/*.h
	$(CXX) -std=c++17 -O1 -Wall -Wextra -Werror $(SAN) -Istubs -I$(FW) \
	  -o $@ sim_device.cpp $(FW)/session_log.cpp

sim: sim_device

test: test_session_log sim_device
	./test_session_log
	./test_end_to_end.bash

clean:
	rm -f test_session_log sim_device
