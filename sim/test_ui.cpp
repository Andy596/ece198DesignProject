// UI and protocol tests for the merged ece198Codes1.0.2 sketch.
//
// The real sketch runs in-process on a virtual clock (delay() advances
// millis()), with Serial connected to an in-memory "laptop". Buttons and the
// knob are driven like a person would: press, release, turn, centre.
// Invariants are checked after every loop() call.
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Arduino.h"
#include "ece198Codes1.0.2.ino"  // the real sketch, unchanged

// --- In-memory serial ------------------------------------------------------------
namespace {
std::string g_toDevice;
size_t g_toDevicePos = 0;
std::string g_fromDevice;
}  // namespace

SimSerial Serial;
void SimSerial::begin(unsigned long) {}
int SimSerial::available() { return static_cast<int>(g_toDevice.size() - g_toDevicePos); }
int SimSerial::read() {
  if (g_toDevicePos >= g_toDevice.size()) return -1;
  return static_cast<unsigned char>(g_toDevice[g_toDevicePos++]);
}
size_t SimSerial::write(const uint8_t* data, size_t len) {
  g_fromDevice.append(reinterpret_cast<const char*>(data), len);
  return len;
}

namespace {

int g_failures = 0;
int g_checks = 0;
int g_invariantFailures = 0;
const char* g_test = "";

#define CHECK(cond)                                                              \
  do {                                                                           \
    ++g_checks;                                                                  \
    if (!(cond)) {                                                               \
      ++g_failures;                                                              \
      fprintf(stderr, "FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond); \
      fprintf(stderr, "     LCD |%s|%s|  screen=%d\n", lcd.line(0), lcd.line(1), screen); \
    }                                                                            \
  } while (0)

void invariantFail(const char* what) {
  if (g_invariantFailures < 10) {
    fprintf(stderr, "INVARIANT [%s] %s (screen=%d user=%d row=%d)\n", g_test, what, screen,
            selectedUser, selectedExerciseRow);
  }
  ++g_invariantFailures;
}

void checkInvariants() {
  if (screen > ScoreScreen) invariantFail("screen out of range");
  const int users = pstore::count();
  if (users > 0 && (selectedUser < 0 || selectedUser >= users)) invariantFail("selectedUser out of range");
  if (screen != UsersScreen) {
    if (currentProfile() == nullptr) invariantFail("menu open without a profile");
    if (selectedExerciseRow < 0 || selectedExerciseRow >= exerciseMenuSize())
      invariantFail("selectedExerciseRow out of range");
  }
  if (screen == ScoreScreen && selectedAssignment() == nullptr) invariantFail("editing nothing");
  if (draftScore < 0 || draftScore > maxScore) invariantFail("draftScore out of range");
  if (oled.overflowCount() != 0) invariantFail("OLED text clipped or drawn without a buffer");
  if (slog::entryCount() > slog::capacity()) invariantFail("too many entries");
  for (uint16_t i = 0; i < slog::entryCount(); ++i) {
    slog::Entry e;
    if (slog::getEntry(i, e) != slog::Status::Ok) invariantFail("getEntry failed");
    if (e.exerciseId >= catalogCount) invariantFail("entry with bad exercise");
    if (e.technique > maxScore) invariantFail("entry with bad score");
    if (e.value != 0) invariantFail("entry with nonzero value");
    if (e.participantId == 0) invariantFail("entry with participant 0");
  }
  for (uint8_t p = 0; p < pstore::count(); ++p) {
    const pstore::Profile* pr = pstore::get(p);
    if (pr->count > catalogCount) invariantFail("profile with too many exercises");
    const size_t l = strlen(pr->label);
    if (l < 1 || l > pstore::kLabelMax) invariantFail("bad label");
  }
}

// --- Driving the device ------------------------------------------------------------

void run(uint32_t ms) {
  const uint32_t end = g_simVirtualMs + ms;
  while (static_cast<int32_t>(g_simVirtualMs - end) < 0) {
    const uint32_t before = g_simVirtualMs;
    loop();
    if (g_simVirtualMs == before) {
      invariantFail("loop() did not advance time");
      g_simVirtualMs += 1;
    }
    checkInvariants();
  }
}

void setKnob(int pos) { g_simAnalog = knobMax - pos; }
void centre() { setKnob(511); }

void press(int pin) {
  g_simPins[pin] = HIGH;
  run(60);
  g_simPins[pin] = LOW;
  run(60);
}
void enter() { press(A0); }
void back() { press(A1); }

// n single steps; negative turns left.
void scroll(int steps) {
  const int pos = steps < 0 ? 300 : 700;
  const int n = steps < 0 ? -steps : steps;
  for (int i = 0; i < n; ++i) {
    setKnob(pos);
    run(40);
    centre();
    run(40);
  }
}

void boot(bool withOled = true) {
  g_toDevice.clear();
  g_toDevicePos = 0;
  g_fromDevice.clear();
  memset(g_simPins, 0, sizeof g_simPins);
  oled = Adafruit_SSD1306(128, 64, &Wire, -1);
  oled.beginResult = withOled;
  lcd.setRGB(0, 0, 0);
  centre();
  setup();
  run(100);
}

std::string lcd0() { return lcd.line(0); }
std::string lcd1() { return lcd.line(1); }
bool lcdStarts(const char* l0, const char* l1) {
  return lcd0().rfind(l0, 0) == 0 && lcd1().rfind(l1, 0) == 0;
}
std::string oledAll() {
  std::string s;
  for (int r = 0; r < Adafruit_SSD1306::kRows; ++r) {
    s += oled.shownRow(r);
    s += '\n';
  }
  return s;
}
bool oledHas(const char* text) { return oledAll().find(text) != std::string::npos; }

// Opens the participant with this label from the participant list.
bool openUser(const char* label) {
  for (int i = 0; i < pstore::count() + 1; ++i) {
    if (lcd0().rfind(label, 0) == 0 && screen == UsersScreen) {
      enter();
      return screen == ProfileScreen;
    }
    scroll(1);
  }
  return false;
}

// Saves `score` for the highlighted exercise starting in the score editor.
void setDraftAndSave(int score) {
  scroll(score - draftScore);
  enter();
}

// --- The "laptop" ---------------------------------------------------------------------

void laptopSend(const std::string& payload) {
  char crc[8];
  snprintf(crc, sizeof crc, "*%02X\n", slog::crc8(payload.data(), payload.size()));
  g_toDevice += payload + crc;
}

// Device lines since the last call, CRC-checked and stripped.
std::vector<std::string> deviceLines() {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i < g_fromDevice.size(); ++i) {
    if (g_fromDevice[i] != '\n') continue;
    const std::string line = g_fromDevice.substr(start, i - start);
    start = i + 1;
    const size_t star = line.rfind('*');
    bool ok = star != std::string::npos && star + 3 == line.size();
    if (ok) {
      unsigned want = 0;
      ok = sscanf(line.c_str() + star + 1, "%2X", &want) == 1 &&
           slog::crc8(line.data(), star) == want;
    }
    if (!ok) {
      ++g_failures;
      fprintf(stderr, "FAIL [%s] bad line from device: '%s'\n", g_test, line.c_str());
      continue;
    }
    out.push_back(line.substr(0, star));
  }
  g_fromDevice.erase(0, start);
  return out;
}

std::string ask(const std::string& payload, uint32_t ms = 100) {
  laptopSend(payload);
  run(ms);
  const auto lines = deviceLines();
  return lines.empty() ? "" : lines.back();
}

struct SyncedEntry {
  unsigned boot, seq, pid, ex;
  int value;
  unsigned score;
};

// Full SYNC + ACK. Returns the entries the device sent.
std::vector<SyncedEntry> syncEntries(bool sendAck = true) {
  laptopSend("SYNC");
  run(200);
  std::vector<SyncedEntry> got;
  unsigned boot = 0, n = 0, lastSeq = 0;
  bool begun = false, ended = false;
  for (const auto& l : deviceLines()) {
    SyncedEntry e{};
    if (sscanf(l.c_str(), "BEGIN %u %u", &boot, &n) == 2) {
      begun = true;
    } else if (sscanf(l.c_str(), "E %u %u %u %u %d %u", &e.boot, &e.seq, &e.pid, &e.ex, &e.value,
                      &e.score) == 6) {
      got.push_back(e);
      lastSeq = e.seq;
    } else if (l.rfind("END ", 0) == 0) {
      ended = true;
    }
  }
  CHECK(begun && ended && got.size() == n);
  if (sendAck && n > 0) {
    const std::string r = ask("ACK " + std::to_string(boot) + " " + std::to_string(lastSeq));
    CHECK(r.rfind("DONE ", 0) == 0);
  }
  return got;
}

struct TestProfile {
  unsigned id;
  std::string label;
  std::vector<std::pair<int, int>> exercises;  // exercise, score
};

// Sends a full list in lockstep like sync.bash. Returns the final reply.
std::string pushProfiles(const std::vector<TestProfile>& list) {
  std::string r = ask("PROFILES " + std::to_string(list.size()));
  if (r != "OK") return r;
  size_t a = 0;
  for (const auto& p : list) {
    r = ask("P " + std::to_string(p.id) + " " + p.label);
    if (r != "OK") return r;
    for (const auto& ex : p.exercises) {
      r = ask("A " + std::to_string(p.id) + " " + std::to_string(ex.first) + " " +
              std::to_string(ex.second));
      if (r != "OK") return r;
      ++a;
    }
  }
  return ask("PEND " + std::to_string(list.size()) + " " + std::to_string(a));
}

std::vector<TestProfile> demoList() {
  return {{101, "Alex", {{7, -1}}}, {102, "Jamie", {{9, -1}}}, {103, "Taylor", {{12, -1}}}};
}

int scoreOf(uint16_t id, uint8_t ex) {
  const int p = pstore::findById(id);
  if (p < 0) return -99;
  const int slot = pstore::findExercise(static_cast<uint8_t>(p), ex);
  return slot < 0 ? -98 : pstore::get(static_cast<uint8_t>(p))->exercises[slot].score;
}

// --- Tests -----------------------------------------------------------------------------

void testBootScreen() {
  g_test = "boot";
  boot();
  CHECK(screen == UsersScreen);
  CHECK(pstore::count() == 3);
  CHECK(lcdStarts("Alex", "ID 101"));
  CHECK(std::string(oled.shownRow(0)).rfind("CCCare", 0) == 0);
  CHECK(oledHas("> Alex") && oledHas("  Jamie") && oledHas("  Taylor"));
  CHECK(lcd.red() == 255 && lcd.green() == 255 && lcd.blue() == 255);
  CHECK(g_fromDevice.empty());  // nothing is sent until the laptop asks
  CHECK(slog::entryCount() == 0);
  CHECK(oled.shownMarkerX() >= deadZoneLeftX && oled.shownMarkerX() <= deadZoneRightX);
}

void testKnobBar() {
  g_test = "knob bar";
  boot();
  setKnob(0);
  run(40);
  CHECK(oled.shownMarkerX() == knobTrackLeft);
  setKnob(1023);
  run(40);
  CHECK(oled.shownMarkerX() == knobTrackRight);
  setKnob(leftThreshold);
  run(40);
  CHECK(oled.shownMarkerX() == deadZoneLeftX);
  setKnob(rightThreshold);
  run(40);
  CHECK(oled.shownMarkerX() == deadZoneRightX);
  centre();
  run(40);
}

void testScrollUsers() {
  g_test = "scroll users";
  boot();
  scroll(1);
  CHECK(lcdStarts("Jamie", "ID 102"));
  CHECK(oledHas("> Jamie"));
  scroll(1);
  CHECK(lcdStarts("Taylor", "ID 103"));
  scroll(1);
  CHECK(lcdStarts("Alex", "ID 101"));  // wraps
  scroll(-1);
  CHECK(lcdStarts("Taylor", "ID 103"));  // wraps backwards
  back();
  CHECK(screen == UsersScreen && lcdStarts("Taylor", "ID 103"));  // Return does nothing here
}

void testOpenProfile() {
  g_test = "open profile";
  boot();
  CHECK(openUser("Jamie"));
  CHECK(std::string(oled.shownRow(0)).rfind("Jamie", 0) == 0);
  CHECK(oledHas("  < Back") && oledHas("> Lunges") && oledHas("  + Add exercise"));
  CHECK(lcdStarts("Press to edit", "No score yet"));
  scroll(-1);
  CHECK(lcdStarts("Jamie", "Press to return"));
  scroll(-1);  // clamps at the top
  CHECK(selectedExerciseRow == 0);
  scroll(2);
  CHECK(lcdStarts("Jamie", "Press to browse"));
  scroll(3);  // clamps at the bottom
  CHECK(selectedExerciseRow == 2);
  scroll(-2);
  enter();  // the Back row
  CHECK(screen == UsersScreen && lcdStarts("Jamie", "ID 102"));
  CHECK(openUser("Jamie"));
  back();  // the Return button
  CHECK(screen == UsersScreen && lcdStarts("Jamie", "ID 102"));
}

void testSaveScoreLogsEntry() {
  g_test = "save score";
  boot();
  CHECK(openUser("Jamie"));
  enter();
  CHECK(screen == ScoreScreen);
  CHECK(lcdStarts("Score 0/100", "Press to save"));
  CHECK(oledHas("Edit score") && oledHas("Jamie") && oledHas("> Lunges") && oledHas("Left -  Right +"));
  scroll(5);
  CHECK(lcdStarts("Score 5/100", "Press to save"));
  CHECK(slog::entryCount() == 0);  // nothing logged while editing
  enter();
  CHECK(screen == ProfileScreen);
  CHECK(lcdStarts("Score saved", "1 to sync"));
  CHECK(slog::entryCount() == 1);
  slog::Entry e;
  CHECK(slog::getEntry(0, e) == slog::Status::Ok);
  CHECK(e.participantId == 102 && e.exerciseId == 9 && e.technique == 5 && e.value == 0 && !e.synced);
  run(statusTime + 100);
  CHECK(lcdStarts("Press to edit", "Score 5/100"));
  // The editor reopens at the saved score.
  enter();
  CHECK(lcdStarts("Score 5/100", "Press to save"));
  scroll(-2);
  enter();
  CHECK(slog::entryCount() == 2);
  CHECK(scoreOf(102, 9) == 3);
  back();
  CHECK(lcdStarts("Jamie", "ID 102 2 unsent"));
}

void testSameScoreTwiceLogsTwice() {
  g_test = "save twice";
  boot();
  CHECK(openUser("Alex"));
  enter();
  setDraftAndSave(40);
  enter();
  enter();  // save 40 again without changing it
  CHECK(slog::entryCount() == 2);
  slog::Entry a, b;
  slog::getEntry(0, a);
  slog::getEntry(1, b);
  CHECK(a.technique == 40 && b.technique == 40 && a.seq != b.seq);
}

void testCancelEdit() {
  g_test = "cancel edit";
  boot();
  CHECK(openUser("Alex"));
  enter();
  setDraftAndSave(60);
  enter();
  scroll(-20);
  CHECK(lcdStarts("Score 40/100", "Press to save"));
  back();
  CHECK(screen == ProfileScreen);
  CHECK(lcdStarts("Press to edit", "Score 60/100"));
  CHECK(slog::entryCount() == 1);
  CHECK(scoreOf(101, 7) == 60);
}

void testScoreLimits() {
  g_test = "score limits";
  boot();
  CHECK(openUser("Taylor"));
  enter();
  scroll(-3);
  CHECK(draftScore == 0 && lcdStarts("Score 0/100", ""));
  setKnob(1023);  // hold far right: fast repeat
  run(20000);
  centre();
  run(40);
  CHECK(draftScore == 100);
  CHECK(lcdStarts("Score 100/100", "Press to save"));
  enter();
  CHECK(scoreOf(103, 12) == 100);
  enter();
  setKnob(0);  // hold far left
  run(20000);
  centre();
  run(40);
  CHECK(draftScore == 0);
  CHECK(lcdStarts("Score 0/100  ", "Press to save"));  // no leftover digits
  enter();
  CHECK(scoreOf(103, 12) == 0);
  CHECK(slog::entryCount() == 2);
}

void testEnterNeedsCentredKnob() {
  g_test = "enter needs centre";
  boot();
  setKnob(700);
  run(40);  // one step to Jamie
  g_simPins[A0] = HIGH;
  run(60);
  g_simPins[A0] = LOW;
  run(60);
  CHECK(screen == UsersScreen);  // ignored while turned
  centre();
  run(40);
  enter();
  CHECK(screen == ProfileScreen);
}

void testReturnWorksWhileTurned() {
  g_test = "return while turned";
  boot();
  CHECK(openUser("Alex"));
  enter();
  setKnob(1023);
  run(100);
  back();
  CHECK(screen == ProfileScreen);
  CHECK(slog::entryCount() == 0);
  centre();
  run(40);
}

void testHeldButtonIsOnePress() {
  g_test = "held button";
  boot();
  g_simPins[A0] = HIGH;
  run(3000);
  CHECK(screen == ProfileScreen && selectedExerciseRow == 1);  // opened once, not more
  g_simPins[A0] = LOW;
  run(100);
  CHECK(screen == ProfileScreen);
  // A bounce shorter than the debounce time is ignored.
  g_simPins[A0] = HIGH;
  run(20);
  g_simPins[A0] = LOW;
  run(100);
  CHECK(screen == ProfileScreen);
}

void testAddExerciseFromCatalog() {
  g_test = "add exercise";
  boot();
  CHECK(openUser("Alex"));
  scroll(1);
  CHECK(lcdStarts("Alex", "Press to browse"));
  enter();
  CHECK(screen == CatalogScreen);
  CHECK(std::string(oled.shownRow(0)).rfind("Add exercise", 0) == 0);
  CHECK(lcdStarts("Alex", "Press to add"));  // Arm rainbows
  scroll(7);  // row 8 = Goblet squats, already assigned
  CHECK(oledHas("> Goblet squats"));
  CHECK(lcdStarts("Alex", "Already added"));
  enter();
  CHECK(screen == CatalogScreen);  // nothing happens
  scroll(39 - 7);  // row 40 = Plank (exercise 39)
  CHECK(oledHas("> Plank"));
  enter();
  CHECK(screen == ProfileScreen);
  CHECK(oledHas("> Plank"));
  CHECK(lcdStarts("Press to edit", "No score yet"));
  CHECK(slog::entryCount() == 0);  // adding is not a saved score
  CHECK(pstore::get(0)->count == 2);
  enter();
  setDraftAndSave(12);
  slog::Entry e;
  CHECK(slog::getEntry(0, e) == slog::Status::Ok && e.exerciseId == 39 && e.technique == 12);
  // Return from the catalog lands on "+ Add exercise".
  scroll(1);
  enter();
  CHECK(screen == CatalogScreen);
  back();
  CHECK(screen == ProfileScreen && lcdStarts("Alex", "Press to browse"));
  // Back row in the catalog does the same.
  enter();
  scroll(-1);
  CHECK(lcdStarts("Alex", "Press to return"));
  enter();
  CHECK(screen == ProfileScreen && lcdStarts("Alex", "Press to browse"));
}

void testLongNamesWrap() {
  g_test = "long names";
  boot();
  CHECK(openUser("Alex"));
  scroll(1);
  enter();
  scroll(1);  // Standing hip opener/closer
  CHECK(oledHas("> Standing hip") && oledHas("  opener/closer"));
  scroll(catalogCount);  // to the end; every row drawn on the way
  CHECK(oledHas("> Neck side stretch"));
  CHECK(oled.overflowCount() == 0);
}

void testEveryCatalogRowFits() {
  g_test = "catalog rows fit";
  boot();
  CHECK(openUser("Alex"));
  scroll(1);
  enter();
  for (int i = 0; i < catalogCount; ++i) {
    CHECK(std::string(oled.shownRow(0)).rfind("Add exercise", 0) == 0);
    scroll(1);
  }
  CHECK(oled.overflowCount() == 0);
  CHECK(lcd.overflowCount() == 0);
}

void testSyncProtocol() {
  g_test = "sync";
  boot();
  CHECK(openUser("Alex"));
  enter();
  setDraftAndSave(86);
  back();
  CHECK(openUser("Jamie"));
  enter();
  setDraftAndSave(92);
  back();
  const unsigned boot = slog::bootId();
  const auto got = syncEntries(false);
  CHECK(got.size() == 2);
  CHECK(got[0].boot == boot && got[0].pid == 101 && got[0].ex == 7 && got[0].score == 86 && got[0].value == 0);
  CHECK(got[1].pid == 102 && got[1].ex == 9 && got[1].score == 92);
  CHECK(lcdStarts("Syncing...", "Keep plugged in"));
  CHECK(lcd.blue() == 255 && lcd.red() == 0);
  // A score saved while the laptop is still saving is kept for next time.
  CHECK(openUser("Taylor"));
  enter();
  setDraftAndSave(50);
  const std::string done = ask("ACK " + std::to_string(boot) + " " + std::to_string(got[1].seq));
  CHECK(done == "DONE " + std::to_string(boot) + " 1 1");
  CHECK(lcdStarts("Sync complete", "2 saved"));
  CHECK(slog::unsyncedCount() == 1);
  const auto again = syncEntries();
  CHECK(again.size() == 1 && again[0].pid == 103 && again[0].score == 50);
  CHECK(slog::safeToPowerOff());
  CHECK(pushProfiles(demoList()) == "PDONE 3 3");
  CHECK(lcdStarts("Profiles loaded", "Safe to unplug"));
  CHECK(lcd.green() == 255 && lcd.red() == 0);
  run(statusTime + 100);
  CHECK(lcd.red() == 255 && lcd.green() == 255 && lcd.blue() == 255);
  CHECK(screen == UsersScreen);
}

void testAckTimeoutShown() {
  g_test = "ack timeout";
  boot();
  CHECK(openUser("Alex"));
  enter();
  setDraftAndSave(10);
  syncEntries(false);
  run(slog::kAckTimeoutMs + 200);
  CHECK(lcdStarts("Sync failed", "Try again"));
  CHECK(slog::unsyncedCount() == 1);
  run(statusTime + 100);
  CHECK(lcd.red() == 255 && lcd.green() == 255);
  CHECK(syncEntries().size() == 1);
  CHECK(slog::safeToPowerOff());
}

void testPushReplacesList() {
  g_test = "push replaces";
  boot();
  scroll(1);  // select Jamie
  const std::vector<TestProfile> list = {
      {50, "Morgan L", {{39, 70}, {0, -1}}}, {102, "Jamie R", {{9, 88}}}, {300, "Sam", {}}};
  CHECK(pushProfiles(list) == "PDONE 3 3");
  CHECK(pstore::count() == 3);
  CHECK(screen == UsersScreen);
  CHECK(lcdStarts("Profiles loaded", "Safe to unplug"));
  run(statusTime + 100);
  CHECK(lcdStarts("Jamie R", "ID 102"));  // same participant still selected
  CHECK(oledHas("> Jamie R") && oledHas("  Morgan L") && oledHas("  Sam"));
  enter();
  CHECK(lcdStarts("Press to edit", "Score 88/100"));
  back();
  scroll(1);
  enter();  // Sam: no exercises
  CHECK(lcdStarts("Sam", "Press to browse"));
  CHECK(oledHas("> + Add exercise"));
  CHECK(selectedExerciseRow == 1);
  back();
}

void testPushDuringEdit() {
  g_test = "push during edit";
  boot();
  CHECK(openUser("Taylor"));
  enter();
  scroll(30);  // unsaved draft
  CHECK(pushProfiles(demoList()) == "PDONE 3 3");
  CHECK(screen == UsersScreen);
  CHECK(slog::entryCount() == 0);  // the draft was not saved
  run(statusTime + 100);
  CHECK(lcdStarts("Taylor", "ID 103"));
  // Same with the catalog open.
  enter();
  scroll(1);
  enter();
  CHECK(screen == CatalogScreen);
  CHECK(pushProfiles(demoList()) == "PDONE 3 3");
  CHECK(screen == UsersScreen);
}

void testPushKeepsUnsyncedScores() {
  g_test = "push keeps unsynced";
  boot();
  CHECK(openUser("Alex"));
  enter();
  setDraftAndSave(77);
  scroll(1);
  enter();
  scroll(39);
  enter();  // add Plank
  enter();
  setDraftAndSave(15);
  back();
  CHECK(slog::unsyncedCount() == 2);
  // The laptop has not seen these scores yet.
  CHECK(pushProfiles({{101, "Alex", {{7, 20}}}, {102, "Jamie", {}}}) == "PDONE 2 1");
  CHECK(scoreOf(101, 7) == 77);   // newer device score wins
  CHECK(scoreOf(101, 39) == 15);  // exercise added back
  CHECK(slog::unsyncedCount() == 2);  // nothing lost
  CHECK(lcdStarts("Profiles loaded", "2 to sync"));
}

void testPushRemovesSelected() {
  g_test = "push removes selected";
  boot();
  scroll(2);  // Taylor
  CHECK(pushProfiles({{101, "Alex", {}}, {102, "Jamie", {}}}) == "PDONE 2 0");
  CHECK(selectedUser == 0);
  run(statusTime + 100);
  CHECK(lcdStarts("Alex", "ID 101"));
  scroll(-1);
  CHECK(lcdStarts("Jamie", "ID 102"));
}

void testEmptyListScreen() {
  g_test = "empty list";
  boot();
  CHECK(pushProfiles({}) == "PDONE 0 0");
  run(statusTime + 100);
  CHECK(lcdStarts("No profiles", "Sync with laptop"));
  CHECK(oledHas("No profiles yet."));
  scroll(3);
  scroll(-5);
  enter();
  back();
  setKnob(1023);
  run(2000);
  centre();
  run(100);
  CHECK(screen == UsersScreen && lcdStarts("No profiles", "Sync with laptop"));
  CHECK(pushProfiles({{9, "Pat", {{1, 4}}}}) == "PDONE 1 1");
  run(statusTime + 100);
  CHECK(lcdStarts("Pat", "ID 9"));
  enter();
  CHECK(lcdStarts("Press to edit", "Score 4/100"));
}

void testBadPushesChangeNothing() {
  g_test = "bad pushes";
  const std::vector<std::vector<TestProfile>> bad = {
      {{0, "Zero", {}}},
      {{5, "Bad*Label", {}}},
      {{5, "Way too long name", {}}},
      {{5, " Lead", {}}},
      {{5, "A", {{55, 1}}}},
      {{5, "A", {{1, 101}}}},
      {{5, "A", {{1, 5}, {1, 6}}}},
      {{5, "A", {}}, {5, "B", {}}},
  };
  for (const auto& list : bad) {
    boot();
    scroll(1);
    const std::string r = pushProfiles(list);
    CHECK(r.rfind("ERR ", 0) == 0);
    CHECK(pstore::count() == 3 && strcmp(pstore::get(1)->label, "Jamie") == 0);
    CHECK(!pstore::receiving());
    run(statusTime + 100);
    CHECK(screen == UsersScreen && lcdStarts("Jamie", "ID 102"));
  }
  boot();
  CHECK(ask("PROFILES 11") == "ERR bad_profiles");
  CHECK(ask("PEND 0 0") == "ERR not_receiving");
  CHECK(ask("PROFILES 1") == "OK");
  CHECK(ask("P 4 Kim") == "OK");
  CHECK(ask("PEND 1 5") == "ERR count_mismatch");
  CHECK(pstore::count() == 3);
  // A line with a broken CRC is refused.
  g_toDevice += "PROFILES 1*00\n";
  run(100);
  auto lines = deviceLines();
  CHECK(!lines.empty() && lines.back() == "ERR bad_crc");
  // More than 11 profiles is impossible to start.
  CHECK(ask("PROFILES 99999") == "ERR bad_profiles");
}

void testStalledPush() {
  g_test = "stalled push";
  boot();
  CHECK(ask("PROFILES 2") == "OK");
  CHECK(ask("P 7 Lee") == "OK");
  // The laptop stops (cable pulled). The device stays usable meanwhile.
  scroll(1);
  CHECK(lcdStarts("Jamie", "ID 102"));
  enter();
  enter();
  setDraftAndSave(33);
  CHECK(slog::entryCount() == 1);
  run(pstore::kStageTimeoutMs + 100);
  CHECK(!pstore::receiving());
  CHECK(pstore::count() == 3);
  CHECK(ask("PEND 2 0") == "ERR not_receiving");
  CHECK(pushProfiles({{7, "Lee", {}}}) == "PDONE 1 0");
  CHECK(pstore::count() == 1);
}

void testMaximumList() {
  g_test = "max list";
  boot();
  std::vector<TestProfile> list;
  for (int p = 0; p < pstore::kMaxProfiles; ++p) {
    TestProfile tp{static_cast<unsigned>(901 + p), "Person " + std::string(1, static_cast<char>('A' + p)), {}};
    for (int e = 0; e < catalogCount; ++e) tp.exercises.emplace_back(e, (e * 3 + p) % 101);
    list.push_back(tp);
  }
  list[9].label = "ABCDEFGHIJKL";  // 12 characters, the longest label
  CHECK(pushProfiles(list) == "PDONE 10 550");
  run(statusTime + 100);
  for (int p = 0; p < pstore::kMaxProfiles; ++p) {
    CHECK(lcdStarts(list[static_cast<size_t>(p)].label.c_str(), ("ID " + std::to_string(901 + p)).c_str()));
    scroll(1);
  }
  scroll(-1);
  CHECK(oledHas("> ABCDEFGHIJKL"));
  enter();
  CHECK(std::string(oled.shownRow(0)).rfind("ABCDEFGHIJKL", 0) == 0);
  scroll(catalogCount - 1);  // from row 1 to the last exercise row
  CHECK(oledHas("> Neck side stretch"));
  scroll(1);
  enter();  // catalog: everything already added
  for (int i = 0; i < catalogCount; ++i) {
    scroll(1);
    CHECK(lcdStarts("ABCDEFGHIJKL", "Already added"));
  }
  enter();
  CHECK(screen == CatalogScreen);
  back();
  CHECK(lcd.overflowCount() == 0);
}

void testDeviceFull() {
  g_test = "device full";
  boot();
  CHECK(openUser("Alex"));
  enter();
  for (int i = 0; i < slog::kCapacity; ++i) {
    enter();  // save the draft unchanged
    enter();  // reopen the editor
  }
  CHECK(slog::entryCount() == slog::kCapacity);
  scroll(9);
  enter();
  CHECK(lcdStarts("Device full!", "Sync to laptop"));
  CHECK(lcd.red() == 255 && lcd.green() == 0);
  CHECK(scoreOf(101, 7) == 0);  // the shown score did not change
  CHECK(slog::entryCount() == slog::kCapacity);
  run(statusTime + 100);
  CHECK(lcd.red() == 255 && lcd.green() == 255);
  CHECK(syncEntries().size() == slog::kCapacity);
  enter();
  setDraftAndSave(9);
  CHECK(lcdStarts("Score saved", "1 to sync"));
  CHECK(scoreOf(101, 7) == 9);
}

void testMissingOled() {
  g_test = "missing OLED";
  boot(false);
  CHECK(lcdStarts("OLED not found", "Check wiring"));
  CHECK(lcd.red() == 255 && lcd.green() == 0);
  scroll(2);
  enter();
  back();
  CHECK(lcdStarts("OLED not found", "Check wiring"));
  // Sync still works, so nothing would be stranded.
  laptopSend("SYNC");
  run(100);
  const auto lines = deviceLines();
  CHECK(!lines.empty() && lines.front().rfind("BEGIN ", 0) == 0);
  CHECK(pushProfiles(demoList()) == "PDONE 3 3");
  run(statusTime + 100);
  CHECK(lcdStarts("OLED not found", "Check wiring"));
  CHECK(oled.overflowCount() == 0);  // the OLED was never touched
}

void testSerialNoise() {
  g_test = "serial noise";
  boot();
  scroll(1);
  srand(7);
  for (int i = 0; i < 300; ++i) {
    std::string junk;
    const int n = rand() % 70;
    for (int k = 0; k < n; ++k) junk += static_cast<char>(rand() % 256);
    g_toDevice += junk + "\n";
    run(20);
  }
  g_toDevice += "hello\n";  // a person typing in the Serial Monitor
  run(100);
  deviceLines();  // every reply must still be CRC-framed
  CHECK(pstore::count() == 3);
  CHECK(slog::entryCount() == 0);
  run(statusTime + 100);
  CHECK(screen == UsersScreen && lcdStarts("Jamie", "ID 102"));
  // The protocol still works afterwards.
  CHECK(pushProfiles(demoList()) == "PDONE 3 3");
}

void testMillisWrap() {
  g_test = "millis wrap";
  boot();
  g_simVirtualMs = 0xFFFFFFFFU - 3000U;
  run(100);
  CHECK(openUser("Alex"));
  enter();
  scroll(4);
  enter();  // the status timer runs across the wrap
  CHECK(lcdStarts("Score saved", "1 to sync"));
  run(statusTime + 3000);
  CHECK(g_simVirtualMs < 0x10000000U);  // wrapped
  CHECK(lcdStarts("Press to edit", "Score 4/100"));
  setKnob(1023);
  run(3000);
  centre();
  run(40);
  CHECK(screen == ProfileScreen);
  CHECK(syncEntries().size() == 1);
}

std::string readFile(const char* path) {
  std::ifstream f(path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

std::vector<std::string> fileLines(const char* path) {
  std::vector<std::string> out;
  std::stringstream s(readFile(path));
  std::string line;
  while (std::getline(s, line)) out.push_back(line);
  return out;
}

// Parses "  (12, 'Name'')s')," -> id and unescaped name.
bool parseExerciseRow(const std::string& line, int& id, std::string& name) {
  int consumed = 0;
  if (sscanf(line.c_str(), " (%d, '%n", &id, &consumed) != 1 || consumed == 0) return false;
  name.clear();
  for (size_t i = static_cast<size_t>(consumed); i < line.size(); ++i) {
    if (line[i] == '\'') {
      if (i + 1 < line.size() && line[i + 1] == '\'') {
        name += '\'';
        ++i;
      } else {
        return line.compare(i, 2, "')") == 0;
      }
    } else {
      name += line[i];
    }
  }
  return false;
}

void testCatalogMatchesSchema() {
  g_test = "catalog vs schema";
  const auto lines = fileLines("../laptop/schema.sql");
  CHECK(!lines.empty());
  int n = 0;
  bool allMatch = true;
  for (const auto& line : lines) {
    int id = 0;
    std::string name;
    if (!parseExerciseRow(line, id, name)) continue;
    if (id != n || id >= catalogCount || name != exerciseCatalog[id]) {
      fprintf(stderr, "  schema row %d '%s' does not match the sketch\n", id, name.c_str());
      allMatch = false;
    }
    ++n;
  }
  CHECK(allMatch);
  CHECK(n == catalogCount);
}

void testDemoProfilesMatchSchema() {
  g_test = "demo vs schema";
  boot();
  std::vector<TestProfile> seed;
  for (const auto& line : fileLines("../laptop/schema.sql")) {
    unsigned id = 0, ex = 0;
    char label[32] = {0};
    if (sscanf(line.c_str(), " SELECT %u, '%31[^']' WHERE", &id, label) == 2) {
      seed.push_back({id, label, {}});
    } else if (sscanf(line.c_str(), " SELECT %u, %u WHERE", &id, &ex) == 2) {
      for (auto& p : seed) {
        if (p.id == id) p.exercises.emplace_back(static_cast<int>(ex), -1);
      }
    }
  }
  CHECK(seed.size() == 3);
  CHECK(seed.size() == pstore::count());
  for (size_t i = 0; i < seed.size() && i < pstore::count(); ++i) {
    const pstore::Profile* p = pstore::get(static_cast<uint8_t>(i));
    CHECK(p->id == seed[i].id && seed[i].label == p->label);
    CHECK(p->count == seed[i].exercises.size());
    for (size_t e = 0; e < seed[i].exercises.size() && e < p->count; ++e) {
      CHECK(p->exercises[e].exerciseId == seed[i].exercises[e].first && p->exercises[e].score == -1);
    }
  }
}

// Random use for a long time: knob, buttons, syncs, profile downloads, noise.
void testFuzz() {
  g_test = "fuzz";
  boot();
  srand(2026);
  int saves = 0;
  for (int step = 0; step < 40000; ++step) {
    const int r = rand() % 100;
    if (r < 25) {
      scroll(rand() % 7 - 3);
    } else if (r < 45) {
      const bool editing = screen == ScoreScreen;
      const uint16_t before = slog::entryCount();
      const bool full = before == slog::kCapacity;
      enter();
      if (editing && !full) {
        ++saves;
        if (slog::entryCount() != before + 1) invariantFail("save did not log exactly one entry");
      }
    } else if (r < 55) {
      back();
    } else if (r < 62) {
      setKnob(rand() % 1024);
      run(static_cast<uint32_t>(rand() % 1500));
      centre();
      run(40);
    } else if (r < 70) {
      run(static_cast<uint32_t>(rand() % 3000));
    } else if (r < 75) {
      syncEntries(rand() % 4 != 0);
    } else if (r < 80) {
      std::vector<TestProfile> list;
      const int n = rand() % (pstore::kMaxProfiles + 1);
      for (int p = 0; p < n; ++p) {
        TestProfile tp{static_cast<unsigned>(1 + rand() % 400 + p * 400),
                       "P" + std::to_string(rand() % 100000), {}};
        const int ex = rand() % 8;
        for (int e = 0; e < ex; ++e) tp.exercises.emplace_back((e * 7 + p) % catalogCount, rand() % 102 - 1);
        list.push_back(tp);
      }
      pushProfiles(list);
    } else if (r < 83) {
      // A push that dies part way.
      ask("PROFILES 3");
      ask("P 77 Half");
      if (rand() % 2) run(pstore::kStageTimeoutMs + 50);
    } else if (r < 86) {
      std::string junk;
      const int n = rand() % 60;
      for (int k = 0; k < n; ++k) junk += static_cast<char>(rand() % 256);
      g_toDevice += junk + "\n";
      run(40);
    } else if (r < 88) {
      ask("ACK " + std::to_string(rand() % 5) + " " + std::to_string(rand() % 70));
    } else {
      run(20);
    }
    deviceLines();
    if (g_invariantFailures > 0) break;
  }
  CHECK(saves > 100);
  CHECK(oled.overflowCount() == 0);
}

}  // namespace

int main() {
  g_simVirtualTime = true;
  testBootScreen();
  testKnobBar();
  testScrollUsers();
  testOpenProfile();
  testSaveScoreLogsEntry();
  testSameScoreTwiceLogsTwice();
  testCancelEdit();
  testScoreLimits();
  testEnterNeedsCentredKnob();
  testReturnWorksWhileTurned();
  testHeldButtonIsOnePress();
  testAddExerciseFromCatalog();
  testLongNamesWrap();
  testEveryCatalogRowFits();
  testSyncProtocol();
  testAckTimeoutShown();
  testPushReplacesList();
  testPushDuringEdit();
  testPushKeepsUnsyncedScores();
  testPushRemovesSelected();
  testEmptyListScreen();
  testBadPushesChangeNothing();
  testStalledPush();
  testMaximumList();
  testDeviceFull();
  testMissingOled();
  testSerialNoise();
  testMillisWrap();
  testCatalogMatchesSchema();
  testDemoProfilesMatchSchema();
  testFuzz();

  g_test = "invariants";
  CHECK(g_invariantFailures == 0);
  if (g_failures != 0) {
    printf("ui: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
  }
  printf("ui: %d/%d checks passed (invariants checked after every loop)\n", g_checks, g_checks);
  return 0;
}
