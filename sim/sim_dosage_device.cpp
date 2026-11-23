// sim_dosage_device.cpp - runs the real ece198Codes108 sketch on a host.
//
// Serial is a pseudo-terminal (stubs/sim_pty.cpp), so laptop/sync_dosages.py
// talks to this simulator exactly as it would to the board over USB. EEPROM
// is backed by a file, so restarting the simulator is a power cycle. Button,
// touch and knob commands fast-forward the sketch's clock; serial traffic runs
// in real time.
//
// Usage: sim_dosage_device [--ready-file F] [--eeprom F] [--mute]
//                          [--drop-completed-once] [--stall-writes] [--no-usb]
// stdin commands (each prints exactly one line):
//   enter | back | touch    press and release Enter (A0), Return (A1), touch (A3)
//   scroll <n>              n single steps (negative = left), knob left centred
//   wait <ms>               run the sketch for ms of device time
//   screen                  LCD and OLED text
//   entries                 completed entries held in RAM
//   identity                device ID and boot counter (1.0.8)
//   usb <0|1>               whether a program has the USB port open
//   quit
#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
// The real sketch, unchanged. Build with -DSKETCH='"ece198Codes1.0.7.ino"' for 1.0.7.
#ifndef SKETCH
#define SKETCH "ece198Codes108.ino"
#endif
#include SKETCH
#include "sim_pty.h"

namespace {

constexpr int kCentre = 511;

void setKnob(int pos) { g_simAnalog = knobMax - pos; }

// Runs the sketch for ms of device time without waiting in real time.
void fastForward(int ms) {
  for (int t = 0; t < ms; t += 10) {
    g_simOffsetMs += 10;
    loop();
    lcd.flushToLog();
  }
}

void press(int pin) {
  g_simPins[pin] = HIGH;
  fastForward(60);
  g_simPins[pin] = LOW;
  fastForward(60);
}

void scroll(long steps) {
  const int pos = steps < 0 ? 300 : 700;  // just past the dead zone: slow repeat
  const long n = steps < 0 ? -steps : steps;
  for (long i = 0; i < n; ++i) {
    setKnob(pos);
    fastForward(40);
    setKnob(kCentre);
    fastForward(40);
  }
}

void printDosage(const Dosage& d) {
  printf("%u/%u/%s/%u/%s", d.sets, d.amount, d.measure == Reps ? "reps" : "sec",
         d.loadQuarters, d.loadUnit == Kilograms ? "kg" : "lb");
}

void handleCommand(char* line) {
  char cmd[16] = {0};
  long a = 0;
  const int n = sscanf(line, "%15s %ld", cmd, &a);
  if (n < 1) {
    return;
  }
  if (strcmp(cmd, "enter") == 0) {
    press(A0);
    printf("OK enter\n");
  } else if (strcmp(cmd, "back") == 0) {
    press(A1);
    printf("OK back\n");
  } else if (strcmp(cmd, "touch") == 0) {
    press(A3);
    printf("OK touch\n");
  } else if (strcmp(cmd, "scroll") == 0 && n == 2) {
    scroll(a);
    printf("OK scroll\n");
  } else if (strcmp(cmd, "wait") == 0 && n == 2 && a >= 0) {
    fastForward(static_cast<int>(a));
    printf("OK wait\n");
  } else if (strcmp(cmd, "screen") == 0) {
    printf("SCREEN %d LCD |%s|%s| OLED", static_cast<int>(screen), lcd.line(0), lcd.line(1));
    for (int r = 0; r < Adafruit_SSD1306::kRows; ++r) printf(" |%s", oled.shownRow(r));
    printf("|\n");
  } else if (strcmp(cmd, "entries") == 0) {
    printf("ENTRIES %d", completedCount);
    for (int i = 0; i < completedCount; ++i) {
      const CompletedEntry& e = completedEntries[i];
      printf("; %lu %s S%lu %s plan=", e.participantId, e.participant, e.session, e.exercise);
      printDosage(e.prescribed);
      printf(" done=");
      printDosage(e.actual);
    }
    printf("\n");
  } else if (strcmp(cmd, "usb") == 0 && n == 2) {
    g_simUsbConnected = a != 0;  // 0: no program has the port open (DTR off)
    printf("OK usb\n");
#ifdef HAS_IDENTITY
  } else if (strcmp(cmd, "identity") == 0) {
    printf("IDENTITY %08lX %lu\n", static_cast<unsigned long>(identity.deviceId),
           static_cast<unsigned long>(identity.boot));
#endif
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
    else if (strcmp(argv[i], "--mute") == 0) g_simFaults.mute = true;
    else if (strcmp(argv[i], "--drop-completed-once") == 0) g_simFaults.dropCompletedOnce = true;
    else if (strcmp(argv[i], "--stall-writes") == 0) g_simFaults.stallWrites = true;
    else if (strcmp(argv[i], "--no-usb") == 0) g_simUsbConnected = false;
    else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
  }
  if (!simPtyOpen(readyFile)) {
    return 1;
  }
  setKnob(kCentre);
  setup();
  for (;;) {
    simPtyPump(2, handleCommand);
    loop();
    lcd.flushToLog();
  }
}
