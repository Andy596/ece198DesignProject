// sim_device.cpp - runs the real SessionLogStation sketch on a Linux/macOS host.
//
// Serial is connected to a pseudo-terminal, so laptop/sync.bash talks to the
// simulator exactly as it would to the board over USB. The LCD is printed to
// stderr; the knob and buttons are driven by commands on stdin.
//
// Usage: sim_device [--ready-file F] [--eeprom F] [--drop-ack-once]
//                   [--corrupt-entry-once] [--mute]
// stdin commands:  add <pid> <exercise> <value> <technique> | undo |
//                  knob <0-1023> | status | quit
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "Arduino.h"
#include "SessionLogStation.ino"  // the real sketch, unchanged

namespace {

int g_master = -1;
const char* g_eepromPath = nullptr;
bool g_dropAckOnce = false;
bool g_corruptEntryOnce = false;
bool g_mute = false;
int g_knob = 0;

// Bytes received from the "laptop" waiting for Serial.read().
constexpr size_t kRxSize = 4096;
uint8_t g_rx[kRxSize];
size_t g_rxHead = 0;
size_t g_rxCount = 0;
char g_rxLine[256];
size_t g_rxLineLen = 0;

void rxPush(const char* data, size_t len) {
  for (size_t i = 0; i < len && g_rxCount < kRxSize; ++i) {
    g_rx[(g_rxHead + g_rxCount) % kRxSize] = static_cast<uint8_t>(data[i]);
    ++g_rxCount;
  }
}

// Reassemble lines so we can simulate a lost ACK.
void rxFromLaptop(char c) {
  if (g_rxLineLen < sizeof g_rxLine) {
    g_rxLine[g_rxLineLen++] = c;
  }
  if (c != '\n') {
    return;
  }
  if (g_dropAckOnce && g_rxLineLen >= 3 && memcmp(g_rxLine, "ACK", 3) == 0) {
    g_dropAckOnce = false;
    fprintf(stderr, "SIM: dropped ACK\n");
  } else {
    rxPush(g_rxLine, g_rxLineLen);
  }
  g_rxLineLen = 0;
}

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
    g_knob = static_cast<int>(a);
    printf("KNOB %d\n", g_knob);
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

// --- Arduino API stand-ins ---------------------------------------------------
SimSerial Serial;
SimEeprom EEPROM;

uint32_t millis() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

int analogRead(int) { return g_knob; }

void SimSerial::begin(unsigned long) {}

int SimSerial::available() { return static_cast<int>(g_rxCount); }

int SimSerial::read() {
  if (g_rxCount == 0) {
    return -1;
  }
  const int c = g_rx[g_rxHead];
  g_rxHead = (g_rxHead + 1) % kRxSize;
  --g_rxCount;
  return c;
}

size_t SimSerial::write(const uint8_t* data, size_t len) {
  if (g_mute) {
    return len;
  }
  char buf[128];
  if (len > sizeof buf) {
    return 0;
  }
  memcpy(buf, data, len);
  if (g_corruptEntryOnce && len > 4 && buf[0] == 'E' && buf[1] == ' ') {
    g_corruptEntryOnce = false;
    buf[2] = (buf[2] == '9') ? '8' : static_cast<char>(buf[2] + 1);  // flip a digit
    fprintf(stderr, "SIM: corrupted an entry line\n");
  }
  size_t done = 0;
  while (done < len) {
    const ssize_t w = ::write(g_master, buf + done, len - done);
    if (w < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      return done;
    }
    done += static_cast<size_t>(w);
  }
  return len;
}

void SimEeprom::load() {
  if (loaded_) return;
  memset(bytes_, 0xFF, sizeof bytes_);  // erased flash reads as 0xFF
  if (g_eepromPath != nullptr) {
    FILE* f = fopen(g_eepromPath, "rb");
    if (f != nullptr) {
      if (fread(bytes_, 1, sizeof bytes_, f) != sizeof bytes_) {
        memset(bytes_, 0xFF, sizeof bytes_);  // short/corrupt file: treat as erased
      }
      fclose(f);
    }
  }
  loaded_ = true;
}

void SimEeprom::save() {
  if (g_eepromPath == nullptr) return;
  FILE* f = fopen(g_eepromPath, "wb");
  if (f != nullptr) {
    if (fwrite(bytes_, 1, sizeof bytes_, f) != sizeof bytes_) {
      fprintf(stderr, "SIM: eeprom write failed\n");
    }
    fclose(f);
  }
}

void rgb_lcd::begin(uint8_t, uint8_t) { clear(); memset(logged_, 0, sizeof logged_); }
void rgb_lcd::setRGB(uint8_t r, uint8_t g, uint8_t b) { r_ = r; g_ = g; b_ = b; }
void rgb_lcd::clear() {
  memset(screen_, ' ', sizeof screen_);
  screen_[0][16] = screen_[1][16] = '\0';
  col_ = row_ = 0;
}
void rgb_lcd::setCursor(uint8_t col, uint8_t row) { col_ = col; row_ = row < 2 ? row : 1; }
void rgb_lcd::print(const char* text) {
  for (; *text != '\0' && col_ < 16; ++text) screen_[row_][col_++] = *text;
}
void rgb_lcd::print(int value) {
  char buf[12];
  snprintf(buf, sizeof buf, "%d", value);
  print(buf);
}
void rgb_lcd::flushToLog() {
  if (memcmp(screen_, logged_, sizeof screen_) == 0 && r_ == loggedR_ && g_ == loggedG_ &&
      b_ == loggedB_) {
    return;
  }
  memcpy(logged_, screen_, sizeof screen_);
  loggedR_ = r_; loggedG_ = g_; loggedB_ = b_;
  fprintf(stderr, "LCD [%3u,%3u,%3u] |%s|%s|\n", r_, g_, b_, screen_[0], screen_[1]);
}

// --- Simulator main loop -----------------------------------------------------
int main(int argc, char** argv) {
  const char* readyFile = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--ready-file") == 0 && i + 1 < argc) readyFile = argv[++i];
    else if (strcmp(argv[i], "--eeprom") == 0 && i + 1 < argc) g_eepromPath = argv[++i];
    else if (strcmp(argv[i], "--drop-ack-once") == 0) g_dropAckOnce = true;
    else if (strcmp(argv[i], "--corrupt-entry-once") == 0) g_corruptEntryOnce = true;
    else if (strcmp(argv[i], "--mute") == 0) g_mute = true;
    else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
  }
  signal(SIGPIPE, SIG_IGN);

  g_master = posix_openpt(O_RDWR | O_NOCTTY);
  if (g_master < 0 || grantpt(g_master) != 0 || unlockpt(g_master) != 0) {
    perror("pty");
    return 1;
  }
  const char* slavePath = ptsname(g_master);
  // Keep our own handle on the slave so the pty survives the laptop script
  // closing it between runs, and make it raw like a USB CDC port.
  const int slave = open(slavePath, O_RDWR | O_NOCTTY);
  termios t;
  if (slave < 0 || tcgetattr(slave, &t) != 0) {
    perror("slave");
    return 1;
  }
  cfmakeraw(&t);
  tcsetattr(slave, TCSANOW, &t);

  if (readyFile != nullptr) {
    FILE* f = fopen(readyFile, "w");
    if (f == nullptr) { perror("ready-file"); return 1; }
    fprintf(f, "%s\n", slavePath);
    fclose(f);
  } else {
    printf("PORT %s\n", slavePath);
    fflush(stdout);
  }

  setup();

  bool stdinOpen = true;
  char cmdLine[128];
  size_t cmdLen = 0;
  for (;;) {
    pollfd fds[2] = {{g_master, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
    const int ready = poll(fds, stdinOpen ? 2 : 1, 2);
    if (ready > 0 && (fds[0].revents & POLLIN)) {
      char buf[256];
      const ssize_t n = ::read(g_master, buf, sizeof buf);
      for (ssize_t i = 0; i < n; ++i) rxFromLaptop(buf[i]);
    }
    if (stdinOpen && ready > 0 && (fds[1].revents & (POLLIN | POLLHUP))) {
      char c;
      const ssize_t n = ::read(STDIN_FILENO, &c, 1);
      if (n <= 0) {
        stdinOpen = false;
      } else if (c == '\n') {
        cmdLine[cmdLen] = '\0';
        handleCommand(cmdLine);
        cmdLen = 0;
      } else if (cmdLen + 1 < sizeof cmdLine) {
        cmdLine[cmdLen++] = c;
      }
    }
    loop();
    lcd.flushToLog();
  }
}
