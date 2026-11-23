// sim_device.cpp - runs the SessionLogStation transfer demo sketch on a host.
//
// Serial is a pseudo-terminal (stubs/sim_pty.cpp). The LCD is printed to
// stderr; the knob is driven by commands on stdin.
//
// Usage: sim_device [--ready-file F] [--eeprom F] [--drop-ack-once]
//                   [--corrupt-entry-once] [--mute]
// stdin commands:  add <pid> <exercise> <value> <technique> | undo |
//                  knob <0-1023> | status | quit
#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
#include "SessionLogStation.ino"  // the real sketch, unchanged
#include "sim_pty.h"

namespace {

void handleCommand(char* line) {
  char cmd[16] = {0};
  long a = 0, b = 0, c = 0, d = 0;
  const int n = sscanf(line, "%15s %ld %ld %ld %ld", cmd, &a, &b, &c, &d);
  if (n < 1) {
    return;
  }
  if (strcmp(cmd, "add") == 0 && n == 5) {
    const bool ok = confirmEntry(static_cast<uint16_t>(a), static_cast<uint8_t>(b),
                                 static_cast<int16_t>(c), static_cast<uint8_t>(d));
    printf("ADD %s\n", ok ? "ok" : "rejected");
  } else if (strcmp(cmd, "undo") == 0) {
    printf("UNDO %s\n", undoLastEntry() ? "ok" : "rejected");
  } else if (strcmp(cmd, "knob") == 0 && n == 2) {
    g_simAnalog = static_cast<int>(a);
    printf("KNOB %d\n", g_simAnalog);
  } else if (strcmp(cmd, "status") == 0) {
    printf("STATE boot=%lu count=%u unsynced=%u safe=%d\n",
           static_cast<unsigned long>(slog::bootId()), slog::entryCount(),
           slog::unsyncedCount(), slog::safeToPowerOff() ? 1 : 0);
  } else if (strcmp(cmd, "quit") == 0) {
    exit(0);
  } else {
    printf("ERR unknown sim command\n");
  }
  fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
  const char* readyFile = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--ready-file") == 0 && i + 1 < argc) readyFile = argv[++i];
    else if (strcmp(argv[i], "--eeprom") == 0 && i + 1 < argc) g_simEepromPath = argv[++i];
    else if (strcmp(argv[i], "--drop-ack-once") == 0) g_simFaults.dropAckOnce = true;
    else if (strcmp(argv[i], "--corrupt-entry-once") == 0) g_simFaults.corruptEntryOnce = true;
    else if (strcmp(argv[i], "--mute") == 0) g_simFaults.mute = true;
    else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
  }
  if (!simPtyOpen(readyFile)) {
    return 1;
  }
  setup();
  for (;;) {
    simPtyPump(2, handleCommand);
    loop();
    lcd.flushToLog();
  }
}
