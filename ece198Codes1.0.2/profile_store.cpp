// profile_store.cpp - see profile_store.h for the API and wire format.
#include "profile_store.h"

#include <stdio.h>
#include <string.h>

namespace pstore {
namespace {

constexpr size_t kCmdMax = 63;  // longest line handled (slog limits lines to 48)

Profile g_live[kMaxProfiles];
uint8_t g_liveCount = 0;

// The list being received. It replaces g_live only on a matching PEND.
Profile g_stage[kMaxProfiles];
uint8_t g_stageCount = 0;
uint8_t g_stageExpected = 0;
uint16_t g_stageAssigned = 0;
bool g_receiving = false;
uint32_t g_lastLineMs = 0;

bool g_updated = false;
ReplyFn g_reply = nullptr;

void reply(const char* text) {
  if (g_reply != nullptr) {
    g_reply(text);
  }
}

// Any error discards the half-received list; the live list is untouched.
void fail(const char* reason) {
  g_receiving = false;
  char buf[32];
  const int n = snprintf(buf, sizeof buf, "ERR %s", reason);
  if (n > 0 && static_cast<size_t>(n) < sizeof buf) {
    reply(buf);
  }
}

// Parses a decimal number at p, advances p. Rejects empty input and values > max.
bool parseUint(const char*& p, uint32_t max, uint32_t& out) {
  uint32_t v = 0;
  const char* start = p;
  while (*p >= '0' && *p <= '9') {
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (v > (max - digit) / 10U) {
      return false;
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

// Accepts "-1" or 0..kMaxScore.
bool parseScore(const char*& p, int8_t& out) {
  if (*p == '-') {
    if (p[1] != '1' || (p[2] >= '0' && p[2] <= '9')) {
      return false;
    }
    p += 2;
    out = kNoScore;
    return true;
  }
  uint32_t v = 0;
  if (!parseUint(p, static_cast<uint32_t>(kMaxScore), v)) {
    return false;
  }
  out = static_cast<int8_t>(v);
  return true;
}

bool skipSpace(const char*& p) {
  if (*p != ' ') {
    return false;
  }
  ++p;
  return true;
}

int findIn(const Profile* list, uint8_t n, uint16_t id) {
  for (uint8_t i = 0; i < n; ++i) {
    if (list[i].id == id) {
      return i;
    }
  }
  return -1;
}

int findSlot(const Profile& p, uint8_t exerciseId) {
  for (uint8_t i = 0; i < p.count; ++i) {
    if (p.exercises[i].exerciseId == exerciseId) {
      return i;
    }
  }
  return -1;
}

// "PROFILES <n>"
void handleStart(const char* p, uint32_t nowMs) {
  uint32_t n = 0;
  if (!skipSpace(p) || !parseUint(p, kMaxProfiles, n) || *p != '\0') {
    fail("bad_profiles");
    return;
  }
  g_receiving = true;
  g_stageCount = 0;
  g_stageExpected = static_cast<uint8_t>(n);
  g_stageAssigned = 0;
  g_lastLineMs = nowMs;
  reply("OK");
}

// "P <id> <label>"
void handleProfile(const char* p, uint32_t nowMs) {
  if (!g_receiving) {
    fail("not_receiving");
    return;
  }
  uint32_t id = 0;
  if (!skipSpace(p) || !parseUint(p, 0xFFFFU, id) || id == 0U || !skipSpace(p)) {
    fail("bad_id");
    return;
  }
  const size_t labelLen = strlen(p);
  if (labelLen == 0U || labelLen > kLabelMax || p[0] == ' ' || p[labelLen - 1U] == ' ') {
    fail("bad_label");
    return;
  }
  for (size_t i = 0; i < labelLen; ++i) {
    if (!isLabelChar(p[i])) {
      fail("bad_label");
      return;
    }
  }
  if (g_stageCount >= g_stageExpected) {
    fail("too_many");
    return;
  }
  if (findIn(g_stage, g_stageCount, static_cast<uint16_t>(id)) >= 0) {
    fail("duplicate_id");
    return;
  }
  Profile& dst = g_stage[g_stageCount];
  dst.id = static_cast<uint16_t>(id);
  memcpy(dst.label, p, labelLen);
  dst.label[labelLen] = '\0';
  dst.count = 0;
  ++g_stageCount;
  g_lastLineMs = nowMs;
  reply("OK");
}

// "A <id> <exercise> <score>"
void handleAssign(const char* p, uint32_t nowMs) {
  if (!g_receiving) {
    fail("not_receiving");
    return;
  }
  uint32_t id = 0;
  uint32_t ex = 0;
  int8_t score = kNoScore;
  if (!skipSpace(p) || !parseUint(p, 0xFFFFU, id) || !skipSpace(p) ||
      !parseUint(p, 0xFFU, ex) || !skipSpace(p) || !parseScore(p, score) || *p != '\0') {
    fail("bad_assignment");
    return;
  }
  if (ex >= kExerciseCount) {
    fail("bad_exercise");
    return;
  }
  const int idx = findIn(g_stage, g_stageCount, static_cast<uint16_t>(id));
  if (idx < 0) {
    fail("unknown_id");
    return;
  }
  Profile& dst = g_stage[idx];
  if (findSlot(dst, static_cast<uint8_t>(ex)) >= 0) {
    fail("duplicate_exercise");
    return;
  }
  // count < kExerciseCount is guaranteed: every exercise ID is distinct and < kExerciseCount.
  dst.exercises[dst.count] = Assigned{static_cast<uint8_t>(ex), score};
  ++dst.count;
  ++g_stageAssigned;
  g_lastLineMs = nowMs;
  reply("OK");
}

// "PEND <n> <assignments>"
void handleEnd(const char* p) {
  if (!g_receiving) {
    fail("not_receiving");
    return;
  }
  uint32_t n = 0;
  uint32_t a = 0;
  if (!skipSpace(p) || !parseUint(p, kMaxProfiles, n) || !skipSpace(p) ||
      !parseUint(p, 0xFFFFU, a) || *p != '\0') {
    fail("bad_end");
    return;
  }
  if (n != g_stageExpected || n != g_stageCount || a != g_stageAssigned) {
    fail("count_mismatch");
    return;
  }
  for (uint8_t i = 0; i < g_stageCount; ++i) {
    g_live[i] = g_stage[i];
  }
  g_liveCount = g_stageCount;
  g_receiving = false;
  g_updated = true;

  char buf[32];
  const int len = snprintf(buf, sizeof buf, "PDONE %u %u", static_cast<unsigned>(n),
                           static_cast<unsigned>(a));
  if (len > 0 && static_cast<size_t>(len) < sizeof buf) {
    reply(buf);
  }
}

// Returns the text after `word` if line is exactly `word` or starts with "word ".
const char* matchWord(const char* line, const char* word) {
  const size_t n = strlen(word);
  if (strncmp(line, word, n) != 0) {
    return nullptr;
  }
  return (line[n] == '\0' || line[n] == ' ') ? line + n : nullptr;
}

}  // namespace

bool isLabelChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == ' ' || c == '.' || c == ',' || c == '\'' || c == '-';
}

void begin(ReplyFn replyFn) {
  g_liveCount = 0;
  g_stageCount = 0;
  g_stageExpected = 0;
  g_stageAssigned = 0;
  g_receiving = false;
  g_lastLineMs = 0;
  g_updated = false;
  g_reply = replyFn;
}

void setReply(ReplyFn replyFn) { g_reply = replyFn; }

bool handleCommand(const char* payload, size_t len, bool crcValid, uint32_t nowMs) {
  if (payload == nullptr || len == 0U) {
    return false;
  }
  // Work on a bounded, NUL-terminated copy.
  char line[kCmdMax + 1];
  const size_t n = (len < kCmdMax) ? len : kCmdMax;
  memcpy(line, payload, n);
  line[n] = '\0';

  const char* rest = nullptr;
  enum { Start, Prof, Assign, End } kind;
  if ((rest = matchWord(line, "PROFILES")) != nullptr) {
    kind = Start;
  } else if ((rest = matchWord(line, "PEND")) != nullptr) {
    kind = End;
  } else if ((rest = matchWord(line, "P")) != nullptr) {
    kind = Prof;
  } else if ((rest = matchWord(line, "A")) != nullptr) {
    kind = Assign;
  } else {
    return false;
  }
  // Every profile command changes state, so it must carry a valid CRC.
  if (!crcValid) {
    fail("bad_crc");
    return true;
  }
  if (len > kCmdMax) {
    fail("line_too_long");
    return true;
  }
  switch (kind) {
    case Start: handleStart(rest, nowMs); break;
    case Prof: handleProfile(rest, nowMs); break;
    case Assign: handleAssign(rest, nowMs); break;
    case End: handleEnd(rest); break;
  }
  return true;
}

void poll(uint32_t nowMs) {
  // Unsigned subtraction stays correct when millis() wraps.
  if (g_receiving && (nowMs - g_lastLineMs) >= kStageTimeoutMs) {
    g_receiving = false;
  }
}

bool receiving() { return g_receiving; }

bool takeUpdated() {
  const bool was = g_updated;
  g_updated = false;
  return was;
}

uint8_t count() { return g_liveCount; }

const Profile* get(uint8_t index) {
  return (index < g_liveCount) ? &g_live[index] : nullptr;
}

int findById(uint16_t id) { return findIn(g_live, g_liveCount, id); }

int findExercise(uint8_t index, uint8_t exerciseId) {
  return (index < g_liveCount) ? findSlot(g_live[index], exerciseId) : -1;
}

Result assign(uint8_t index, uint8_t exerciseId) {
  if (index >= g_liveCount || exerciseId >= kExerciseCount) {
    return Result::Invalid;
  }
  Profile& p = g_live[index];
  if (findSlot(p, exerciseId) >= 0) {
    return Result::AlreadyAssigned;
  }
  if (p.count >= kExerciseCount) {
    return Result::Full;  // unreachable while IDs are distinct; kept as a guard
  }
  p.exercises[p.count] = Assigned{exerciseId, kNoScore};
  ++p.count;
  return Result::Ok;
}

Result setScore(uint8_t index, uint8_t exerciseId, int8_t score) {
  if (index >= g_liveCount || exerciseId >= kExerciseCount || score < kNoScore ||
      score > kMaxScore) {
    return Result::Invalid;
  }
  int slot = findSlot(g_live[index], exerciseId);
  if (slot < 0) {
    const Result r = assign(index, exerciseId);
    if (r != Result::Ok) {
      return r;
    }
    slot = g_live[index].count - 1;
  }
  g_live[index].exercises[slot].score = score;
  return Result::Ok;
}

}  // namespace pstore
