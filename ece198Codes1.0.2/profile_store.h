// profile_store.h - participant profiles downloaded from the laptop.
//
// The laptop database is the source of truth for participant IDs, labels and
// assigned exercises. During a sync, laptop/sync.bash sends the full list and
// this module swaps it in only when every line arrived intact, so a broken
// transfer never leaves the device with half a list.
//
// No Arduino-specific code and no heap: the same file runs on the board and in
// the host tests (sim/).
//
// ---------------------------------------------------------------------------
// WIRE FORMAT (laptop -> device; every line ends in "*HH", CRC-8 poly 0x07)
//
//   PROFILES <n>                 start a new list of n profiles (n <= 10)
//   P <id> <label>               one profile; label is the rest of the line
//   A <id> <exercise> <score>    one assigned exercise; score -1 = not scored
//   PEND <n> <assignments>       commit if both counts match
//
// The device answers every line with "OK", or "ERR <reason>" (which also
// discards the list being received), and answers PEND with
// "PDONE <n> <assignments>" once the new list is live.
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pstore {

constexpr uint8_t kMaxProfiles = 10;
constexpr uint8_t kLabelMax = 12;
// Must match exerciseCatalog in the sketch and the exercises in laptop/schema.sql.
constexpr uint8_t kExerciseCount = 55;
constexpr int8_t kNoScore = -1;
constexpr int8_t kMaxScore = 100;
// A half-received list is dropped after this long without a new line.
constexpr uint32_t kStageTimeoutMs = 10000;

struct Assigned {
  uint8_t exerciseId;  // index into the shared exercise catalog
  int8_t score;        // kNoScore, or 0..kMaxScore
};

struct Profile {
  uint16_t id;                     // participant ID from the laptop database
  char label[kLabelMax + 1];       // display name, NUL-terminated
  uint8_t count;                   // assigned exercises in use
  Assigned exercises[kExerciseCount];
};

enum class Result : uint8_t { Ok, AlreadyAssigned, Full, Invalid };

using ReplyFn = void (*)(const char* payload);

// Clears everything. reply may be nullptr (no answers are sent).
void begin(ReplyFn reply);
void setReply(ReplyFn reply);

// Handles one laptop line. Signature matches slog::CommandFn so it can be
// passed to slog::setCommandHandler(). Returns false if the line is not a
// profile command.
bool handleCommand(const char* payload, size_t len, bool crcValid, uint32_t nowMs);
void poll(uint32_t nowMs);   // drops a stalled half-received list
bool receiving();            // a list is being received
bool takeUpdated();          // true once after each new list goes live

// --- Live list (what the UI shows) ------------------------------------------
uint8_t count();
const Profile* get(uint8_t index);           // nullptr if out of range
int findById(uint16_t id);                   // index, or -1
int findExercise(uint8_t index, uint8_t exerciseId);  // slot, or -1
// Adds an unscored exercise to a profile.
Result assign(uint8_t index, uint8_t exerciseId);
// Sets a score, adding the exercise first if the profile does not have it.
Result setScore(uint8_t index, uint8_t exerciseId, int8_t score);

// True if c may appear in a label. Shared with the tests.
bool isLabelChar(char c);

}  // namespace pstore
