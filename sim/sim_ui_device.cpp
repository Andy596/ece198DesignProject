// sim_ui_device.cpp - runs the real ece198Codes1.0.2 sketch on a host.
//
// Serial is a pseudo-terminal (stubs/sim_pty.cpp), so laptop/sync.bash talks to
// this simulator exactly as it would to the board. The LCD log goes to stderr.
// Button and knob commands fast-forward the sketch's clock, so a scripted
// press takes no real time; serial traffic runs in real time.
//
// Usage: sim_ui_device [--ready-file F] [--eeprom F] [--drop-ack-once]
//                      [--corrupt-entry-once] [--mute] [--no-oled]
// stdin commands (each prints one line):
//   enter | back            press and release a button (knob must be centred for enter)
//   scroll <n>              n single steps (negative = left), knob left centred
//   knob <0-1023>           set the knob position (higher = turned right)
//   wait <ms>               run the sketch for ms of device time
//   screen                  print the LCD and OLED text
//   status | profiles | entries | quit
#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
#include "ece198Codes1.0.2.ino"  // the real sketch, unchanged
#include "sim_pty.h"

namespace {

constexpr int kCentre = 511;
int g_knobPos = kCentre;

void setKnob(int pos) {
  g_knobPos = pos;
  g_simAnalog = knobMax - pos;  // the real potentiometer reads lower when turned right
}

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

const char* screenName() {
  switch (screen) {
    case UsersScreen: return "users";
    case ProfileScreen: return "profile";
    case CatalogScreen: return "catalog";
    case ScoreScreen: return "score";
  }
  return "?";
}

void printScreen() {
  printf("SCREEN %s LCD |%s|%s| OLED", screenName(), lcd.line(0), lcd.line(1));
  for (int r = 0; r < Adafruit_SSD1306::kRows; ++r) printf(" |%s", oled.shownRow(r));
  printf("|\n");
}

void printProfiles() {
  printf("PROFILES %u", pstore::count());
  for (uint8_t i = 0; i < pstore::count(); ++i) {
    const pstore::Profile* p = pstore::get(i);
    printf("; %u %s", p->id, p->label);
    for (uint8_t j = 0; j < p->count; ++j) {
      printf(" %u:%d", p->exercises[j].exerciseId, p->exercises[j].score);
    }
  }
  printf("\n");
}

void printEntries() {
  printf("ENTRIES %u", slog::entryCount());
  for (uint16_t i = 0; i < slog::entryCount(); ++i) {
    slog::Entry e;
    if (slog::getEntry(i, e) == slog::Status::Ok) {
      printf(" %u:%u:%u:%u:%s", e.seq, e.participantId, e.exerciseId, e.technique,
             e.synced ? "s" : "u");
    }
  }
  printf("\n");
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
  } else if (strcmp(cmd, "scroll") == 0 && n == 2) {
    scroll(a);
    printf("OK scroll\n");
  } else if (strcmp(cmd, "knob") == 0 && n == 2 && a >= 0 && a <= 1023) {
    setKnob(static_cast<int>(a));
    fastForward(20);
    printf("OK knob\n");
  } else if (strcmp(cmd, "wait") == 0 && n == 2 && a >= 0) {
    fastForward(static_cast<int>(a));
    printf("OK wait\n");
  } else if (strcmp(cmd, "screen") == 0) {
    printScreen();
  } else if (strcmp(cmd, "status") == 0) {
    printf("STATE boot=%lu count=%u unsynced=%u safe=%d profiles=%u screen=%s\n",
           static_cast<unsigned long>(slog::bootId()), slog::entryCount(),
           slog::unsyncedCount(), slog::safeToPowerOff() ? 1 : 0, pstore::count(),
           screenName());
  } else if (strcmp(cmd, "profiles") == 0) {
    printProfiles();
  } else if (strcmp(cmd, "entries") == 0) {
    printEntries();
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
    else if (strcmp(argv[i], "--no-oled") == 0) oled.beginResult = false;
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
