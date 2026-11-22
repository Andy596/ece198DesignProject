// sim_pty.h - Serial backed by a pseudo-terminal, so laptop/sync.bash talks to
// a simulator exactly as it would to the board over USB. Includes fault
// injection for the end-to-end tests.
#pragma once

#include <stddef.h>

struct SimPtyFaults {
  bool dropAckOnce = false;       // swallow the first ACK line from the laptop
  bool corruptEntryOnce = false;  // flip a digit in the first "E ..." line sent
  bool mute = false;              // never send anything
};
extern SimPtyFaults g_simFaults;

// Creates the pty and writes its path to readyFile (or prints "PORT <path>").
// Returns false on failure.
bool simPtyOpen(const char* readyFile);

// Waits up to timeoutMs for laptop bytes or a stdin line, handles what arrived,
// and calls onLine for each complete stdin line. Returns false once stdin closed
// and nothing else can happen.
using SimLineFn = void (*)(char* line);
void simPtyPump(int timeoutMs, SimLineFn onLine);
