// Unit tests for ece198Codes1.0.2/profile_store.cpp (no hardware needed).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "profile_store.h"

namespace {

std::vector<std::string> g_replies;
int g_failures = 0;
int g_checks = 0;

void capture(const char* text) { g_replies.emplace_back(text); }

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      ++g_failures;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)

// Sends one line with a valid CRC; returns the reply ("" if none).
std::string cmd(const char* line, uint32_t now = 0) {
  g_replies.clear();
  const bool handled = pstore::handleCommand(line, strlen(line), true, now);
  if (!handled) return "<not handled>";
  return g_replies.empty() ? "" : g_replies.back();
}

void reset() {
  pstore::begin(capture);
  g_replies.clear();
}

// Loads a list and checks every reply.
bool load(const std::vector<std::string>& lines) {
  bool ok = true;
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string r = cmd(lines[i].c_str());
    const bool last = i + 1 == lines.size();
    if (!last && r != "OK") ok = false;
    if (last && r.rfind("PDONE", 0) != 0) ok = false;
  }
  return ok;
}

void loadDemo() {
  reset();
  CHECK(load({"PROFILES 2", "P 101 Alex", "A 101 7 -1", "A 101 9 55", "P 102 Jamie", "PEND 2 2"}));
  CHECK(pstore::takeUpdated());
}

void testBasicLoad() {
  loadDemo();
  CHECK(pstore::count() == 2);
  const pstore::Profile* p = pstore::get(0);
  CHECK(p != nullptr && p->id == 101 && strcmp(p->label, "Alex") == 0 && p->count == 2);
  CHECK(p->exercises[0].exerciseId == 7 && p->exercises[0].score == -1);
  CHECK(p->exercises[1].exerciseId == 9 && p->exercises[1].score == 55);
  CHECK(pstore::get(1)->count == 0);
  CHECK(pstore::get(2) == nullptr);
  CHECK(pstore::get(255) == nullptr);
  CHECK(pstore::findById(102) == 1);
  CHECK(pstore::findById(999) == -1);
  CHECK(!pstore::takeUpdated());  // reported once
  CHECK(!pstore::receiving());
}

void testEmptyList() {
  loadDemo();
  CHECK(cmd("PROFILES 0") == "OK");
  CHECK(cmd("PEND 0 0") == "PDONE 0 0");
  CHECK(pstore::count() == 0);
  CHECK(pstore::get(0) == nullptr);
  CHECK(pstore::takeUpdated());
}

void testReplaceIsAtomic() {
  loadDemo();
  CHECK(cmd("PROFILES 1") == "OK");
  CHECK(cmd("P 200 Zed") == "OK");
  // Live list is unchanged until PEND.
  CHECK(pstore::count() == 2 && pstore::get(0)->id == 101);
  CHECK(pstore::receiving());
  CHECK(cmd("PEND 1 0") == "PDONE 1 0");
  CHECK(pstore::count() == 1 && pstore::get(0)->id == 200);
}

// Each bad line must answer ERR, abort the list, and leave the live list alone.
void expectReject(const std::vector<std::string>& lines, const char* reason) {
  loadDemo();
  std::string last;
  for (const auto& l : lines) last = cmd(l.c_str());
  const std::string want = std::string("ERR ") + reason;
  if (last != want) {
    fprintf(stderr, "  expected '%s', got '%s' (last line '%s')\n", want.c_str(), last.c_str(),
            lines.back().c_str());
  }
  CHECK(last == want);
  CHECK(!pstore::receiving());
  CHECK(!pstore::takeUpdated());
  CHECK(pstore::count() == 2 && pstore::get(0)->id == 101 && pstore::get(0)->count == 2);
  // A PEND after the error is refused too: the half list is gone.
  CHECK(cmd("PEND 1 0") == "ERR not_receiving");
}

void testRejections() {
  expectReject({"PROFILES 11"}, "bad_profiles");
  expectReject({"PROFILES"}, "bad_profiles");
  expectReject({"PROFILES -1"}, "bad_profiles");
  expectReject({"PROFILES 2 3"}, "bad_profiles");
  expectReject({"PROFILES  2"}, "bad_profiles");
  expectReject({"PROFILES x"}, "bad_profiles");
  expectReject({"PROFILES 99999999999999999999"}, "bad_profiles");
  expectReject({"P 5 Bob"}, "not_receiving");
  expectReject({"A 5 1 1"}, "not_receiving");
  expectReject({"PEND 0 0"}, "not_receiving");
  expectReject({"PROFILES 2", "P 0 Zero"}, "bad_id");
  expectReject({"PROFILES 2", "P 65536 Big"}, "bad_id");
  expectReject({"PROFILES 2", "P -3 Neg"}, "bad_id");
  expectReject({"PROFILES 2", "P 5"}, "bad_id");
  expectReject({"PROFILES 2", "P 5 "}, "bad_label");
  expectReject({"PROFILES 2", "P x Bob"}, "bad_id");
  expectReject({"PROFILES 2", "P 5  Bob"}, "bad_label");      // leading space
  expectReject({"PROFILES 2", "P 5 Bob "}, "bad_label");      // trailing space
  expectReject({"PROFILES 2", "P 5 ABCDEFGHIJKLM"}, "bad_label");  // 13 chars
  expectReject({"PROFILES 2", "P 5 Bob*"}, "bad_label");
  expectReject({"PROFILES 2", "P 5 Bob|x"}, "bad_label");
  expectReject({"PROFILES 2", "P 5 Bo\tb"}, "bad_label");
  expectReject({"PROFILES 2", "P 5 Zoë"}, "bad_label");      // non-ASCII
  expectReject({"PROFILES 1", "P 5 A", "P 6 B"}, "too_many");
  expectReject({"PROFILES 2", "P 5 A", "P 5 B"}, "duplicate_id");
  expectReject({"PROFILES 2", "P 5 A", "A 6 1 1"}, "unknown_id");
  expectReject({"PROFILES 2", "P 5 A", "A 5 55 1"}, "bad_exercise");
  expectReject({"PROFILES 2", "P 5 A", "A 5 255 1"}, "bad_exercise");
  expectReject({"PROFILES 2", "P 5 A", "A 5 256 1"}, "bad_assignment");
  expectReject({"PROFILES 2", "P 5 A", "A 5 1 101"}, "bad_assignment");
  expectReject({"PROFILES 2", "P 5 A", "A 5 1 -2"}, "bad_assignment");
  expectReject({"PROFILES 2", "P 5 A", "A 5 1 -10"}, "bad_assignment");
  expectReject({"PROFILES 2", "P 5 A", "A 5 1 -"}, "bad_assignment");
  expectReject({"PROFILES 2", "P 5 A", "A 5 1"}, "bad_assignment");
  expectReject({"PROFILES 2", "P 5 A", "A 5 1 1 1"}, "bad_assignment");
  expectReject({"PROFILES 2", "P 5 A", "A 5 1 x"}, "bad_assignment");
  expectReject({"PROFILES 2", "P 5 A", "A 5 1 5", "A 5 1 6"}, "duplicate_exercise");
  expectReject({"PROFILES 2", "P 5 A", "PEND 1 0"}, "count_mismatch");
  expectReject({"PROFILES 1", "P 5 A", "PEND 1 1"}, "count_mismatch");
  expectReject({"PROFILES 1", "P 5 A", "A 5 1 1", "PEND 1 0"}, "count_mismatch");
  expectReject({"PROFILES 1", "P 5 A", "PEND 2 0"}, "count_mismatch");
  expectReject({"PROFILES 1", "P 5 A", "PEND 11 0"}, "bad_end");
  expectReject({"PROFILES 1", "P 5 A", "PEND 1"}, "bad_end");
  expectReject({"PROFILES 1", "P 5 A", "PEND 1 0 0"}, "bad_end");
}

void testAcceptedEdgeValues() {
  reset();
  CHECK(load({"PROFILES 2", "P 65535 ABCDEFGHIJKL", "A 65535 0 0", "A 65535 54 100",
              "P 1 O'Neil, J.-Ka", "PEND 2 2"}) == false);  // 13 chars: rejected
  reset();
  CHECK(load({"PROFILES 2", "P 65535 ABCDEFGHIJKL", "A 65535 0 0", "A 65535 54 100",
              "P 1 O'Neil, J.-K", "PEND 2 2"}));
  CHECK(pstore::count() == 2);
  CHECK(strcmp(pstore::get(0)->label, "ABCDEFGHIJKL") == 0);
  CHECK(strcmp(pstore::get(1)->label, "O'Neil, J.-K") == 0);
  CHECK(pstore::get(0)->exercises[1].exerciseId == 54 && pstore::get(0)->exercises[1].score == 100);
  CHECK(pstore::get(0)->exercises[0].score == 0);
}

void testMaximumList() {
  reset();
  std::vector<std::string> lines{"PROFILES 10"};
  char buf[64];
  for (int p = 0; p < 10; ++p) {
    snprintf(buf, sizeof buf, "P %d Person %d", 1000 + p, p);
    lines.emplace_back(buf);
    for (int e = 0; e < pstore::kExerciseCount; ++e) {
      snprintf(buf, sizeof buf, "A %d %d %d", 1000 + p, e, (p * 7 + e) % 101);
      lines.emplace_back(buf);
    }
  }
  lines.emplace_back("PEND 10 550");
  CHECK(load(lines));
  CHECK(pstore::count() == 10);
  for (uint8_t p = 0; p < 10; ++p) {
    CHECK(pstore::get(p)->count == pstore::kExerciseCount);
    // Every exercise is assigned, so nothing more can be added.
    CHECK(pstore::assign(p, 3) == pstore::Result::AlreadyAssigned);
  }
  CHECK(pstore::get(9)->exercises[54].score == (9 * 7 + 54) % 101);
}

void testCrcRequired() {
  loadDemo();
  g_replies.clear();
  const char* line = "PROFILES 1*00";
  CHECK(pstore::handleCommand(line, strlen(line), false, 0));
  CHECK(!g_replies.empty() && g_replies.back() == "ERR bad_crc");
  CHECK(!pstore::receiving());
  // A bad-CRC line in the middle of a list aborts it.
  CHECK(cmd("PROFILES 1") == "OK");
  CHECK(cmd("P 5 A") == "OK");
  g_replies.clear();
  CHECK(pstore::handleCommand("PEND 1 0*ZZ", 11, false, 0));
  CHECK(g_replies.back() == "ERR bad_crc");
  CHECK(cmd("PEND 1 0") == "ERR not_receiving");
  CHECK(pstore::count() == 2);
}

void testNotOurCommands() {
  reset();
  CHECK(cmd("SYNC") == "<not handled>");
  CHECK(cmd("ACK 1 2") == "<not handled>");
  CHECK(cmd("PROFILESX 1") == "<not handled>");
  CHECK(cmd("PX") == "<not handled>");
  CHECK(cmd("AB 1") == "<not handled>");
  CHECK(cmd("PENDING") == "<not handled>");
  CHECK(cmd("p 1 a") == "<not handled>");
  CHECK(!pstore::handleCommand("", 0, true, 0));
  CHECK(!pstore::handleCommand(nullptr, 5, true, 0));
  CHECK(g_replies.empty());
}

void testRestartMidList() {
  loadDemo();
  CHECK(cmd("PROFILES 2") == "OK");
  CHECK(cmd("P 5 A") == "OK");
  // The laptop gave up and started again: the new PROFILES wins.
  CHECK(cmd("PROFILES 1") == "OK");
  CHECK(cmd("P 6 B") == "OK");
  CHECK(cmd("PEND 1 0") == "PDONE 1 0");
  CHECK(pstore::count() == 1 && pstore::get(0)->id == 6);
}

void testTimeout() {
  loadDemo();
  CHECK(cmd("PROFILES 1", 1000) == "OK");
  CHECK(cmd("P 5 A", 5000) == "OK");
  pstore::poll(5000 + pstore::kStageTimeoutMs - 1);
  CHECK(pstore::receiving());  // each line restarts the timer
  pstore::poll(5000 + pstore::kStageTimeoutMs);
  CHECK(!pstore::receiving());
  CHECK(cmd("PEND 1 0") == "ERR not_receiving");
  CHECK(pstore::count() == 2);
  // millis() wrap-around.
  CHECK(cmd("PROFILES 0", 0xFFFFFF00U) == "OK");
  pstore::poll(0x00000010U);
  CHECK(pstore::receiving());
  pstore::poll(0xFFFFFF00U + pstore::kStageTimeoutMs);
  CHECK(!pstore::receiving());
}

void testLiveEdits() {
  loadDemo();
  CHECK(pstore::findExercise(0, 7) == 0);
  CHECK(pstore::findExercise(0, 8) == -1);
  CHECK(pstore::findExercise(5, 7) == -1);
  CHECK(pstore::assign(0, 8) == pstore::Result::Ok);
  CHECK(pstore::findExercise(0, 8) == 2 && pstore::get(0)->exercises[2].score == -1);
  CHECK(pstore::assign(0, 8) == pstore::Result::AlreadyAssigned);
  CHECK(pstore::assign(0, 55) == pstore::Result::Invalid);
  CHECK(pstore::assign(2, 1) == pstore::Result::Invalid);
  CHECK(pstore::setScore(0, 8, 70) == pstore::Result::Ok);
  CHECK(pstore::get(0)->exercises[2].score == 70);
  CHECK(pstore::setScore(1, 30, 0) == pstore::Result::Ok);  // adds the exercise
  CHECK(pstore::get(1)->count == 1 && pstore::get(1)->exercises[0].exerciseId == 30);
  CHECK(pstore::setScore(0, 8, 101) == pstore::Result::Invalid);
  CHECK(pstore::setScore(0, 8, -2) == pstore::Result::Invalid);
  CHECK(pstore::setScore(0, 8, -1) == pstore::Result::Ok);
  CHECK(pstore::setScore(0, 60, 5) == pstore::Result::Invalid);
  CHECK(pstore::setScore(9, 1, 5) == pstore::Result::Invalid);
}

void testLabelChars() {
  int allowed = 0;
  for (int c = -128; c < 128; ++c) {
    if (pstore::isLabelChar(static_cast<char>(c))) ++allowed;
  }
  CHECK(allowed == 26 + 26 + 10 + 5);
  CHECK(!pstore::isLabelChar('*') && !pstore::isLabelChar('|') && !pstore::isLabelChar('\n'));
}

void testNoReplyFunction() {
  pstore::begin(nullptr);
  CHECK(pstore::handleCommand("PROFILES 1", 10, true, 0));
  CHECK(pstore::handleCommand("P 9 X", 5, true, 0));
  CHECK(pstore::handleCommand("PEND 1 0", 8, true, 0));
  CHECK(pstore::count() == 1);
}

void testLongLine() {
  reset();
  std::string longLine = "P 5 " + std::string(200, 'a');
  g_replies.clear();
  CHECK(pstore::handleCommand(longLine.c_str(), longLine.size(), true, 0));
  CHECK(g_replies.back() == "ERR line_too_long");
}

// Random lines must never crash, and the live list must always stay valid.
void testFuzz() {
  reset();
  srand(12345);
  const char* words[] = {"PROFILES", "P", "A", "PEND", "SYNC", "X", ""};
  const char* parts[] = {"0", "1", "5", "10", "11", "-1", "54", "55", "100", "101", "65535",
                         "65536", "Bob", "a b", " ", "", "x", "99999999999", "-", "--1"};
  char line[96];
  for (int i = 0; i < 200000; ++i) {
    int len = snprintf(line, sizeof line, "%s", words[rand() % 7]);
    const int n = rand() % 5;
    for (int k = 0; k < n && len < 80; ++k) {
      len += snprintf(line + len, sizeof line - static_cast<size_t>(len), " %s", parts[rand() % 20]);
    }
    if (rand() % 50 == 0) {
      // Random bytes, including NULs inside the given length.
      len = rand() % 60;
      for (int k = 0; k < len; ++k) line[k] = static_cast<char>(rand() % 256);
      line[len] = '\0';
    }
    pstore::handleCommand(line, static_cast<size_t>(len), rand() % 10 != 0,
                          static_cast<uint32_t>(i) * 7U);
    if (rand() % 100 == 0) pstore::poll(static_cast<uint32_t>(i) * 7U + 20000U);
    if (rand() % 20 == 0) {
      pstore::setScore(static_cast<uint8_t>(rand() % 12), static_cast<uint8_t>(rand() % 60),
                       static_cast<int8_t>(rand() % 104 - 2));
    }
  }
  CHECK(pstore::count() <= pstore::kMaxProfiles);
  for (uint8_t p = 0; p < pstore::count(); ++p) {
    const pstore::Profile* pr = pstore::get(p);
    CHECK(pr->id != 0);
    const size_t l = strlen(pr->label);
    CHECK(l >= 1 && l <= pstore::kLabelMax);
    CHECK(pr->count <= pstore::kExerciseCount);
    for (uint8_t e = 0; e < pr->count; ++e) {
      CHECK(pr->exercises[e].exerciseId < pstore::kExerciseCount);
      CHECK(pr->exercises[e].score >= -1 && pr->exercises[e].score <= 100);
    }
  }
}

}  // namespace

int main() {
  testBasicLoad();
  testEmptyList();
  testReplaceIsAtomic();
  testRejections();
  testAcceptedEdgeValues();
  testMaximumList();
  testCrcRequired();
  testNotOurCommands();
  testRestartMidList();
  testTimeout();
  testLiveEdits();
  testLabelChars();
  testNoReplyFunction();
  testLongLine();
  testFuzz();
  if (g_failures != 0) {
    printf("profile_store: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
  }
  printf("profile_store: %d/%d checks passed\n", g_checks, g_checks);
  return 0;
}
