// Unit tests for SessionLogStation/session_log.cpp (no hardware needed).
#include <stdio.h>
#include <string.h>

#include <string>

#include "session_log.h"

namespace {

std::string g_out;
int g_failures = 0;
int g_checks = 0;

void capture(const char* text, size_t len) { g_out.append(text, len); }

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      ++g_failures;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)

void send(const char* line, uint32_t now = 0) {
  for (const char* p = line; *p != '\0'; ++p) slog::feed(*p, now);
}

std::string framed(const char* payload) {
  char buf[80];
  snprintf(buf, sizeof buf, "%s*%02X\n", payload, slog::crc8(payload, strlen(payload)));
  return buf;
}

bool outHas(const char* s) { return g_out.find(s) != std::string::npos; }

void reset(uint32_t boot = 7) {
  slog::begin(boot, capture);
  g_out.clear();
}

void testCrcKnownValue() {
  // CRC-8/SMBUS check value for "123456789" is 0xF4.
  CHECK(slog::crc8("123456789", 9) == 0xF4);
}

void testAddAndGet() {
  reset();
  CHECK(slog::addEntry(101, 0, 12, 86) == slog::Status::Ok);
  CHECK(slog::addEntry(102, 1, -5, 0) == slog::Status::Ok);
  CHECK(slog::entryCount() == 2);
  CHECK(slog::unsyncedCount() == 2);
  slog::Entry e;
  CHECK(slog::getEntry(1, e) == slog::Status::Ok);
  CHECK(e.participantId == 102 && e.value == -5 && e.seq == 1 && !e.synced);
  CHECK(slog::getEntry(2, e) == slog::Status::Empty);
  CHECK(!slog::safeToPowerOff());
}

void testValidation() {
  slog::begin(1, nullptr);
  CHECK(slog::addEntry(1, 0, 1, 1) == slog::Status::NotStarted);
  reset();
  CHECK(slog::addEntry(1, 0, 1, slog::kMaxTechnique + 1) == slog::Status::Invalid);
  CHECK(slog::entryCount() == 0);
}

void testFullSyncCycle() {
  reset(7);
  slog::addEntry(101, 0, 12, 86);
  slog::addEntry(102, 1, 10, 92);
  send("SYNC\n", 100);
  CHECK(outHas(framed("BEGIN 7 2").c_str()));
  CHECK(outHas(framed("E 7 0 101 0 12 86").c_str()));
  CHECK(outHas(framed("E 7 1 102 1 10 92").c_str()));
  CHECK(outHas(framed("END 7 2").c_str()));
  CHECK(slog::awaitingAck());
  CHECK(slog::takeEvent() == slog::Event::SyncStarted);

  g_out.clear();
  send(framed("ACK 7 1").c_str(), 200);
  CHECK(outHas(framed("DONE 7 1 0").c_str()));
  CHECK(slog::safeToPowerOff());
  CHECK(slog::lastSyncCount() == 2);
  CHECK(slog::takeEvent() == slog::Event::SyncConfirmed);
  slog::Entry e;
  slog::getEntry(0, e);
  CHECK(e.synced);
}

void testEmptySync() {
  reset(3);
  send("SYNC\n");
  CHECK(outHas(framed("BEGIN 3 0").c_str()));
  CHECK(outHas(framed("END 3 0").c_str()));
  CHECK(!slog::awaitingAck());
}

void testAckRules() {
  reset(7);
  slog::addEntry(1, 0, 1, 1);
  send(framed("ACK 7 0").c_str());  // no sync open
  CHECK(outHas("ERR unexpected_ack"));
  CHECK(slog::unsyncedCount() == 1);

  send("SYNC\n");
  g_out.clear();
  send("ACK 7 0\n");  // missing CRC
  CHECK(outHas("ERR bad_crc"));
  g_out.clear();
  send(framed("ACK 8 0").c_str());  // wrong boot
  CHECK(outHas("ERR unexpected_ack"));
  g_out.clear();
  send(framed("ACK 7 5").c_str());  // beyond what was sent
  CHECK(outHas("ERR unexpected_ack"));
  g_out.clear();
  send(framed("ACK").c_str());  // no arguments
  CHECK(outHas("ERR bad_ack"));
  g_out.clear();
  send(framed("ACK 7 99999999999").c_str());  // overflow
  CHECK(outHas("ERR bad_ack"));
  CHECK(slog::unsyncedCount() == 1);
  send(framed("ACK 7 0").c_str());
  CHECK(slog::safeToPowerOff());
}

void testEntryAddedDuringSyncIsNotAcked() {
  reset(7);
  slog::addEntry(1, 0, 1, 1);
  send("SYNC\n");
  slog::addEntry(2, 0, 2, 2);  // arrives after the entries were sent
  send(framed("ACK 7 0").c_str());
  CHECK(slog::unsyncedCount() == 1);
  CHECK(outHas(framed("DONE 7 0 1").c_str()));
}

void testTimeout() {
  reset(7);
  slog::addEntry(1, 0, 1, 1);
  send("SYNC\n", 1000);
  slog::poll(1000 + slog::kAckTimeoutMs - 1);
  CHECK(slog::awaitingAck());
  slog::poll(1000 + slog::kAckTimeoutMs);
  CHECK(!slog::awaitingAck());
  slog::takeEvent();  // SyncStarted
  CHECK(slog::takeEvent() == slog::Event::SyncTimedOut);
  CHECK(slog::unsyncedCount() == 1);
}

void testTimeoutAcrossMillisWrap() {
  reset(7);
  slog::addEntry(1, 0, 1, 1);
  send("SYNC\n", 0xFFFFFF00UL);
  slog::poll(0x00000010UL);  // wrapped, only 0x110 ms later
  CHECK(slog::awaitingAck());
}

void testFullAndCompaction() {
  reset(7);
  for (uint16_t i = 0; i < slog::kCapacity; ++i) {
    CHECK(slog::addEntry(i, 0, 1, 1) == slog::Status::Ok);
  }
  CHECK(slog::addEntry(999, 0, 1, 1) == slog::Status::Full);
  send("SYNC\n");
  CHECK(slog::addEntry(999, 0, 1, 1) == slog::Status::Full);  // no compaction mid-sync
  char ack[24];
  snprintf(ack, sizeof ack, "ACK 7 %u", static_cast<unsigned>(slog::kCapacity - 1U));
  send(framed(ack).c_str());
  CHECK(slog::safeToPowerOff());
  CHECK(slog::addEntry(999, 0, 1, 1) == slog::Status::Ok);  // synced ones dropped
  CHECK(slog::entryCount() == 1);
  slog::Entry e;
  slog::getEntry(0, e);
  CHECK(e.participantId == 999 && e.seq == slog::kCapacity);  // seq keeps counting
}

void testUndo() {
  reset(7);
  CHECK(slog::removeLastEntry() == slog::Status::Empty);
  slog::addEntry(1, 0, 1, 1);
  slog::addEntry(2, 0, 1, 1);
  CHECK(slog::removeLastEntry() == slog::Status::Ok);
  CHECK(slog::entryCount() == 1);
  slog::addEntry(3, 0, 1, 1);
  slog::Entry e;
  slog::getEntry(1, e);
  CHECK(e.seq == 2);  // removed seq 1 is never reused
  send("SYNC\n");
  CHECK(slog::removeLastEntry() == slog::Status::AlreadySynced);  // already sent
  send(framed("ACK 7 2").c_str());
  CHECK(slog::removeLastEntry() == slog::Status::AlreadySynced);
}

void testBadInput() {
  reset(7);
  std::string longLine(200, 'X');
  longLine += "\n";
  send(longLine.c_str());
  CHECK(outHas("ERR line_too_long"));
  g_out.clear();
  send("HELLO\n");
  CHECK(outHas("ERR unknown_command"));
  g_out.clear();
  send("SYNCX\n");
  CHECK(outHas("ERR unknown_command"));
  g_out.clear();
  send("\r\n\n");  // blank lines are ignored
  CHECK(g_out.empty());
  g_out.clear();
  send("STATUS\r\n");
  CHECK(outHas(framed("STATUS 7 0 0").c_str()));
}

void testEventQueueOverflowKeepsLatest() {
  reset(7);
  for (int i = 0; i < 6; ++i) send("BAD\n");
  int n = 0;
  while (slog::takeEvent() != slog::Event::None) ++n;
  CHECK(n == 4);
}

}  // namespace

int main() {
  testCrcKnownValue();
  testAddAndGet();
  testValidation();
  testFullSyncCycle();
  testEmptySync();
  testAckRules();
  testEntryAddedDuringSyncIsNotAcked();
  testTimeout();
  testTimeoutAcrossMillisWrap();
  testFullAndCompaction();
  testUndo();
  testBadInput();
  testEventQueueOverflowKeepsLatest();
  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
