// session_log.h - on-device session storage and USB transfer protocol.
//
// Owner: Noah (data transfer, database sync, exports).
//
// This module holds no Arduino-specific code. The sketch passes it a write
// function and feeds it incoming serial bytes, so the same code runs on the
// board and in the host simulator (sim/sim_device.cpp).
//
// Memory: everything is statically allocated (no heap, no String).
//
// ---------------------------------------------------------------------------
// INTEGRATION HOOKS (for the input/UI code)
//
//   slog::begin(bootId, writeFn)  once in setup()
//   slog::poll(nowMs)             every loop(); also feed bytes with feed()
//   slog::addEntry(...)           when the instructor confirms an entry
//   slog::removeLastEntry()       "undo" for the newest unsynced entry
//   slog::entryCount() / getEntry(i, out)   to show entries on the display
//   slog::unsyncedCount() / safeToPowerOff() for a "sync before off" warning
//   slog::takeEvent()             to show "Syncing..." / "Synced N" / errors
//   slog::setCommandHandler(fn)   optional: route extra laptop commands
//                                 (e.g. profile download) to another module
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace slog {

// Entries kept in RAM. 64 * 8 bytes = 512 bytes of the R4 Minima's 32 KB.
constexpr uint16_t kCapacity = 64;
// Highest technique score accepted. Change it if the team picks a different scale.
constexpr uint8_t kMaxTechnique = 100;
// Longest command line accepted from the laptop (including the CRC suffix).
constexpr size_t kLineMax = 48;
// How long the device waits for an ACK after sending entries.
constexpr uint32_t kAckTimeoutMs = 10000;

struct Entry {
  uint16_t seq;            // unique within this boot; (bootId, seq) is unique overall
  uint16_t participantId;  // assigned ID, never a name
  uint8_t exerciseId;      // index into the shared exercise list
  uint8_t technique;       // 0..kMaxTechnique
  int16_t value;           // reps, seconds, kg... whatever the exercise uses
  bool synced;             // true once the laptop confirmed it is saved
};

enum class Status : uint8_t {
  Ok,
  Full,        // no free slot and no synced entries to drop: sync first
  Invalid,     // an argument was out of range
  Empty,       // nothing to remove / no such entry
  AlreadySynced,  // removeLastEntry() on an entry the laptop already has
  NotStarted,  // begin() was not called
};

enum class Event : uint8_t {
  None,
  SyncStarted,    // laptop asked for entries; entries were sent
  SyncConfirmed,  // laptop saved them; lastSyncCount() says how many
  SyncTimedOut,   // no ACK arrived; entries stay unsynced, retry is safe
  ProtocolError,  // bad command or checksum from the laptop
};

using WriteFn = void (*)(const char* text, size_t len);

// Handler for laptop commands this module does not know. `payload` is
// NUL-terminated and excludes the "*HH" suffix when the CRC was valid; when
// crcValid is false it is the whole raw line. Return true if the command was
// recognised (the handler sends its own reply); false makes this module answer
// "ERR unknown_command".
using CommandFn = bool (*)(const char* payload, size_t len, bool crcValid, uint32_t nowMs);

// Call once. bootId must change on every power-up (the sketch keeps a counter
// in EEPROM) so that the laptop can tell entries from different boots apart.
// begin() clears any command handler; set it afterwards.
void begin(uint32_t bootId, WriteFn write);
void setCommandHandler(CommandFn handler);

// --- Entry hooks ----------------------------------------------------------
Status addEntry(uint16_t participantId, uint8_t exerciseId, int16_t value,
                uint8_t technique);
Status removeLastEntry();
// index 0 = oldest entry still held on the device.
Status getEntry(uint16_t index, Entry& out);
uint16_t entryCount();
uint16_t unsyncedCount();
uint16_t capacity();
bool safeToPowerOff();  // true when every entry is on the laptop

// --- Transfer hooks -------------------------------------------------------
void feed(char c, uint32_t nowMs);  // one byte received from USB serial
void poll(uint32_t nowMs);          // handles the ACK timeout
bool awaitingAck();
Event takeEvent();                  // returns and clears the oldest event
uint16_t lastSyncCount();           // entries confirmed by the last ACK
uint32_t bootId();
// Sends "payload*HH\n" to the laptop. Silently drops payloads that are empty
// or too long for one protocol line.
void sendFramed(const char* payload);

// --- Shared with tests and the laptop script --------------------------------
// CRC-8, polynomial 0x07, initial value 0x00, no reflection.
uint8_t crc8(const char* data, size_t len);

}  // namespace slog
