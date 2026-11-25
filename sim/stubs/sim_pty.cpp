// sim_pty.cpp - see sim_pty.h.
#include "sim_pty.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "Arduino.h"

SimPtyFaults g_simFaults;
SimSerial Serial;

namespace {

int g_master = -1;
bool g_stdinOpen = true;
char g_cmd[256];
size_t g_cmdLen = 0;

// Bytes received from the "laptop" waiting for Serial.read().
constexpr size_t kRxSize = 8192;
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

// Reassemble lines so a lost ACK can be simulated.
void rxFromLaptop(char c) {
  if (g_rxLineLen < sizeof g_rxLine) {
    g_rxLine[g_rxLineLen++] = c;
  }
  if (c != '\n') {
    return;
  }
  if (g_simFaults.dropAckOnce && g_rxLineLen >= 3 && memcmp(g_rxLine, "ACK", 3) == 0) {
    g_simFaults.dropAckOnce = false;
    fprintf(stderr, "SIM: dropped ACK\n");
  } else {
    rxPush(g_rxLine, g_rxLineLen);
  }
  g_rxLineLen = 0;
}

}  // namespace

void SimSerial::begin(unsigned long) {}

SimSerial::operator bool() const { return g_simUsbConnected; }

int SimSerial::availableForWrite() { return g_simFaults.stallWrites ? 0 : 64; }

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
  if (g_simFaults.mute) {
    return len;
  }
  char buf[256];
  if (len > sizeof buf) {
    len = sizeof buf;  // like a USB buffer: accept what fits, the caller retries the rest
  }
  memcpy(buf, data, len);
  if (g_simFaults.dropCompletedOnce && len > 10 && memcmp(buf, "completed,", 10) == 0) {
    g_simFaults.dropCompletedOnce = false;  // report success, send nothing: a lost row
    fprintf(stderr, "SIM: dropped a completed row\n");
    return len;
  }
  if (g_simFaults.corruptEntryOnce && len > 4 && buf[0] == 'E' && buf[1] == ' ') {
    g_simFaults.corruptEntryOnce = false;
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

bool simPtyOpen(const char* readyFile) {
  signal(SIGPIPE, SIG_IGN);
  g_master = posix_openpt(O_RDWR | O_NOCTTY);
  if (g_master < 0 || grantpt(g_master) != 0 || unlockpt(g_master) != 0) {
    perror("pty");
    return false;
  }
  const char* slavePath = ptsname(g_master);
  // Keep our own handle on the slave so the pty survives the laptop script
  // closing it between runs, and make it raw like a USB CDC port.
  const int slave = open(slavePath, O_RDWR | O_NOCTTY);
  termios t;
  if (slave < 0 || tcgetattr(slave, &t) != 0) {
    perror("slave");
    return false;
  }
  cfmakeraw(&t);
  tcsetattr(slave, TCSANOW, &t);

  if (readyFile != nullptr) {
    FILE* f = fopen(readyFile, "w");
    if (f == nullptr) {
      perror("ready-file");
      return false;
    }
    fprintf(f, "%s\n", slavePath);
    fclose(f);
  } else {
    printf("PORT %s\n", slavePath);
    fflush(stdout);
  }
  return true;
}

void simPtyPump(int timeoutMs, SimLineFn onLine) {
  pollfd fds[2] = {{g_master, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
  const int ready = poll(fds, g_stdinOpen ? 2 : 1, timeoutMs);
  if (ready <= 0) {
    return;
  }
  if (fds[0].revents & POLLIN) {
    char buf[256];
    const ssize_t n = ::read(g_master, buf, sizeof buf);
    for (ssize_t i = 0; i < n; ++i) rxFromLaptop(buf[i]);
  }
  if (g_stdinOpen && (fds[1].revents & (POLLIN | POLLHUP))) {
    char c;
    const ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n <= 0) {
      g_stdinOpen = false;
    } else if (c == '\n') {
      g_cmd[g_cmdLen] = '\0';
      onLine(g_cmd);
      g_cmdLen = 0;
    } else if (g_cmdLen + 1 < sizeof g_cmd) {
      g_cmd[g_cmdLen++] = c;
    }
  }
}
