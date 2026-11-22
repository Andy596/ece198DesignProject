// session_log.cpp - see session_log.h for the API and the README for the wire format.
#include "session_log.h"

#include <stdio.h>
#include <string.h>

namespace slog {
namespace {

// Stored form of an entry; `synced` is derived from syncedCount.
struct Stored {
  uint16_t seq;
  uint16_t participantId;
  uint8_t exerciseId;
  uint8_t technique;
  int16_t value;
};

constexpr uint8_t kEventQueueSize = 4;
constexpr size_t kOutMax = 64;  // longest line the device sends

Stored g_entries[kCapacity];
uint16_t g_count = 0;        // entries held
uint16_t g_syncedCount = 0;  // entries [0, g_syncedCount) are on the laptop
uint16_t g_nextSeq = 0;
uint32_t g_bootId = 0;
WriteFn g_write = nullptr;
CommandFn g_handler = nullptr;

char g_line[kLineMax + 1];
size_t g_lineLen = 0;
bool g_lineOverflow = false;

bool g_awaitingAck = false;
uint16_t g_lastSentSeq = 0;
uint32_t g_sentAtMs = 0;
uint16_t g_lastSyncCount = 0;

Event g_events[kEventQueueSize];
uint8_t g_eventHead = 0;
uint8_t g_eventCount = 0;

void pushEvent(Event e) {
  if (g_eventCount == kEventQueueSize) {
    // Drop the oldest so the UI always sees the latest state.
    g_eventHead = static_cast<uint8_t>((g_eventHead + 1U) % kEventQueueSize);
    --g_eventCount;
  }
  const uint8_t tail = static_cast<uint8_t>((g_eventHead + g_eventCount) % kEventQueueSize);
  g_events[tail] = e;
  ++g_eventCount;
}

// Sends "payload*HH\n". Payload is built by the caller with snprintf.
void sendLine(const char* payload, int len) {
  if (g_write == nullptr || len <= 0 || static_cast<size_t>(len) >= kOutMax - 4U) {
    return;  // snprintf failed or truncated: send nothing rather than garbage
  }
  char out[kOutMax];
  const size_t n = static_cast<size_t>(len);
  memcpy(out, payload, n);
  const int tail = snprintf(out + n, kOutMax - n, "*%02X\n",
                            static_cast<unsigned>(crc8(payload, n)));
  if (tail != 4) {
    return;
  }
  g_write(out, n + 4U);
}

void sendError(const char* reason) {
  char buf[kOutMax];
  const int len = snprintf(buf, sizeof buf, "ERR %s", reason);
  sendLine(buf, len);
}

// Parses a decimal number at *p, advances p. Rejects empty input and values > max.
bool parseUint(const char*& p, uint32_t max, uint32_t& out) {
  uint32_t v = 0;
  const char* start = p;
  while (*p >= '0' && *p <= '9') {
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (v > (max - digit) / 10U) {
      return false;  // would exceed max (also prevents uint32 overflow)
    }
    v = v * 10U + digit;
    ++p;
  }
  if (p == start) {
    return false;
  }
  out = v;
  return true;
}

// Splits "payload*HH" and checks the CRC. Returns payload length, or -1.
int checkCrc(const char* line, size_t len) {
  if (len < 4U || line[len - 3U] != '*') {
    return -1;
  }
  uint32_t got = 0;
  for (size_t i = len - 2U; i < len; ++i) {
    const char c = line[i];
    uint32_t nibble;
    if (c >= '0' && c <= '9') {
      nibble = static_cast<uint32_t>(c - '0');
    } else if (c >= 'A' && c <= 'F') {
      nibble = static_cast<uint32_t>(c - 'A' + 10);
    } else if (c >= 'a' && c <= 'f') {
      nibble = static_cast<uint32_t>(c - 'a' + 10);
    } else {
      return -1;
    }
    got = (got << 4) | nibble;
  }
  const size_t payloadLen = len - 3U;
  return (crc8(line, payloadLen) == got) ? static_cast<int>(payloadLen) : -1;
}

// Command name match: the payload equals `cmd` or starts with "cmd ".
bool isCommand(const char* payload, size_t len, const char* cmd) {
  const size_t n = strlen(cmd);
  if (len < n || memcmp(payload, cmd, n) != 0) {
    return false;
  }
  return len == n || payload[n] == ' ';
}

void handleSync(uint32_t nowMs) {
  char buf[kOutMax];
  const uint16_t pending = static_cast<uint16_t>(g_count - g_syncedCount);
  int len = snprintf(buf, sizeof buf, "BEGIN %lu %u",
                     static_cast<unsigned long>(g_bootId), static_cast<unsigned>(pending));
  sendLine(buf, len);
  for (uint16_t i = g_syncedCount; i < g_count; ++i) {
    const Stored& e = g_entries[i];
    len = snprintf(buf, sizeof buf, "E %lu %u %u %u %d %u",
                   static_cast<unsigned long>(g_bootId), static_cast<unsigned>(e.seq),
                   static_cast<unsigned>(e.participantId), static_cast<unsigned>(e.exerciseId),
                   static_cast<int>(e.value), static_cast<unsigned>(e.technique));
    sendLine(buf, len);
  }
  len = snprintf(buf, sizeof buf, "END %lu %u",
                 static_cast<unsigned long>(g_bootId), static_cast<unsigned>(pending));
  sendLine(buf, len);

  if (pending > 0U) {
    g_awaitingAck = true;
    g_lastSentSeq = g_entries[g_count - 1U].seq;
    g_sentAtMs = nowMs;
    pushEvent(Event::SyncStarted);
  }
}

// "ACK <boot> <lastSeq>" - the laptop has committed every entry up to lastSeq.
void handleAck(const char* payload, size_t len) {
  if (len <= 4U) {  // bare "ACK" with no arguments
    sendError("bad_ack");
    pushEvent(Event::ProtocolError);
    return;
  }
  const char* p = payload + 4;  // skip "ACK "
  uint32_t boot = 0;
  uint32_t lastSeq = 0;
  if (!parseUint(p, 0xFFFFFFFFUL, boot) || *p != ' ') {
    sendError("bad_ack");
    pushEvent(Event::ProtocolError);
    return;
  }
  ++p;
  if (!parseUint(p, 0xFFFFU, lastSeq) || *p != '\0') {
    sendError("bad_ack");
    pushEvent(Event::ProtocolError);
    return;
  }
  // Only accept an ACK for entries this boot actually sent in the open sync.
  if (!g_awaitingAck || boot != g_bootId || lastSeq > g_lastSentSeq) {
    sendError("unexpected_ack");
    pushEvent(Event::ProtocolError);
    return;
  }
  uint16_t newlySynced = 0;
  while (g_syncedCount < g_count && g_entries[g_syncedCount].seq <= lastSeq) {
    ++g_syncedCount;
    ++newlySynced;
  }
  g_awaitingAck = false;
  g_lastSyncCount = newlySynced;

  char buf[kOutMax];
  const int outLen = snprintf(buf, sizeof buf, "DONE %lu %u %u",
                              static_cast<unsigned long>(g_bootId),
                              static_cast<unsigned>(lastSeq),
                              static_cast<unsigned>(g_count - g_syncedCount));
  sendLine(buf, outLen);
  pushEvent(Event::SyncConfirmed);
}

void handleStatus() {
  char buf[kOutMax];
  const int len = snprintf(buf, sizeof buf, "STATUS %lu %u %u",
                           static_cast<unsigned long>(g_bootId),
                           static_cast<unsigned>(g_count),
                           static_cast<unsigned>(g_count - g_syncedCount));
  sendLine(buf, len);
}

void handleLine(const char* line, size_t len, uint32_t nowMs) {
  if (len == 0U) {
    return;
  }
  // SYNC and STATUS only read data, so they are accepted with or without a
  // CRC (handy when typing in the Serial Monitor). ACK changes state, so it
  // must carry a valid CRC.
  const int crcLen = checkCrc(line, len);
  const size_t payloadLen = (crcLen >= 0) ? static_cast<size_t>(crcLen) : len;

  char payload[kLineMax + 1];
  memcpy(payload, line, payloadLen);
  payload[payloadLen] = '\0';

  if (isCommand(payload, payloadLen, "SYNC") && payloadLen == 4U) {
    handleSync(nowMs);
  } else if (isCommand(payload, payloadLen, "STATUS") && payloadLen == 6U) {
    handleStatus();
  } else if (isCommand(payload, payloadLen, "ACK")) {
    if (crcLen < 0) {
      sendError("bad_crc");
      pushEvent(Event::ProtocolError);
      return;
    }
    handleAck(payload, payloadLen);
  } else if (g_handler != nullptr && g_handler(payload, payloadLen, crcLen >= 0, nowMs)) {
    // Handled (and answered) by the registered module.
  } else {
    sendError("unknown_command");
    pushEvent(Event::ProtocolError);
  }
}

// Drops synced entries from the front to make room. Synced entries are always
// a prefix because ACKs confirm everything up to a sequence number.
void compact() {
  if (g_syncedCount == 0U) {
    return;
  }
  const uint16_t remaining = static_cast<uint16_t>(g_count - g_syncedCount);
  for (uint16_t i = 0; i < remaining; ++i) {
    g_entries[i] = g_entries[i + g_syncedCount];
  }
  g_count = remaining;
  g_syncedCount = 0;
}

}  // namespace

uint8_t crc8(const char* data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc = static_cast<uint8_t>(crc ^ static_cast<uint8_t>(data[i]));
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      const unsigned shifted = static_cast<unsigned>(crc) << 1U;
      crc = static_cast<uint8_t>((crc & 0x80U) ? (shifted ^ 0x07U) : shifted);
    }
  }
  return crc;
}

void begin(uint32_t bootId, WriteFn write) {
  g_count = 0;
  g_syncedCount = 0;
  g_nextSeq = 0;
  g_bootId = bootId;
  g_write = write;
  g_handler = nullptr;
  g_lineLen = 0;
  g_lineOverflow = false;
  g_awaitingAck = false;
  g_lastSentSeq = 0;
  g_sentAtMs = 0;
  g_lastSyncCount = 0;
  g_eventHead = 0;
  g_eventCount = 0;
}

Status addEntry(uint16_t participantId, uint8_t exerciseId, int16_t value, uint8_t technique) {
  if (g_write == nullptr) {
    return Status::NotStarted;
  }
  if (technique > kMaxTechnique) {
    return Status::Invalid;
  }
  if (g_nextSeq == 0xFFFFU) {
    return Status::Full;  // sequence space for this boot used up; power-cycle after syncing
  }
  if (g_count == kCapacity && !g_awaitingAck) {
    compact();  // never compact mid-sync: the indices of sent entries must not move
  }
  if (g_count == kCapacity) {
    return Status::Full;
  }
  g_entries[g_count] = Stored{g_nextSeq, participantId, exerciseId, technique, value};
  ++g_count;
  ++g_nextSeq;
  return Status::Ok;
}

Status removeLastEntry() {
  if (g_count == 0U) {
    return Status::Empty;
  }
  if (g_count == g_syncedCount) {
    return Status::AlreadySynced;
  }
  if (g_awaitingAck && g_entries[g_count - 1U].seq <= g_lastSentSeq) {
    return Status::AlreadySynced;  // already sent; the laptop may have saved it
  }
  --g_count;
  // The sequence number is not reused, so the laptop never sees two
  // different entries with the same (boot, seq).
  return Status::Ok;
}

Status getEntry(uint16_t index, Entry& out) {
  if (index >= g_count) {
    return Status::Empty;
  }
  const Stored& s = g_entries[index];
  out = Entry{s.seq, s.participantId, s.exerciseId, s.technique, s.value, index < g_syncedCount};
  return Status::Ok;
}

uint16_t entryCount() { return g_count; }
uint16_t unsyncedCount() { return static_cast<uint16_t>(g_count - g_syncedCount); }
uint16_t capacity() { return kCapacity; }
bool safeToPowerOff() { return g_count == g_syncedCount; }
bool awaitingAck() { return g_awaitingAck; }
uint16_t lastSyncCount() { return g_lastSyncCount; }
uint32_t bootId() { return g_bootId; }

void setCommandHandler(CommandFn handler) { g_handler = handler; }

void sendFramed(const char* payload) {
  if (payload == nullptr) {
    return;
  }
  size_t len = 0;
  while (len < kOutMax && payload[len] != '\0') {  // bounded strlen
    ++len;
  }
  sendLine(payload, static_cast<int>(len));
}

void feed(char c, uint32_t nowMs) {
  if (c == '\r') {
    return;
  }
  if (c == '\n') {
    if (g_lineOverflow) {
      sendError("line_too_long");
      pushEvent(Event::ProtocolError);
    } else {
      handleLine(g_line, g_lineLen, nowMs);
    }
    g_lineLen = 0;
    g_lineOverflow = false;
    return;
  }
  if (g_lineLen < kLineMax) {
    g_line[g_lineLen] = c;
    ++g_lineLen;
  } else {
    g_lineOverflow = true;
  }
}

void poll(uint32_t nowMs) {
  // Unsigned subtraction handles millis() wrap-around after ~49 days.
  if (g_awaitingAck && (nowMs - g_sentAtMs) >= kAckTimeoutMs) {
    g_awaitingAck = false;
    pushEvent(Event::SyncTimedOut);
  }
}

Event takeEvent() {
  if (g_eventCount == 0U) {
    return Event::None;
  }
  const Event e = g_events[g_eventHead];
  g_eventHead = static_cast<uint8_t>((g_eventHead + 1U) % kEventQueueSize);
  --g_eventCount;
  return e;
}

}  // namespace slog
