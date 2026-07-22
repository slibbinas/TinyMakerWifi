// ===================================================================================
// Shared UI helpers - the single place the common look is defined.
// All screens draw frames, titles, bottom buttons and action hints through
// these, so a style change is made once here, not per screen.
// Kept OUTSIDE any #if ENABLE_NETWORK guard: used by network-free builds too.
// ===================================================================================

// Standard rounded frame: colored border + black inside (pass ORANGE, RED...)
void uiFrame(uint16_t color) {
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, color);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);
}

// Orange screen title, top-left (About / Update / WiFi Info style)
void uiTitle(const char *text) {
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(ORANGE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 16);
  gfx2->print(text);
  gfx2->setTextColor(WHITE);
}

// One bottom button. slot 0 = left (Back position), 1 = right (OK position).
// The label is centered inside the 72x18 button.
void uiButton(int slot, const char *label, uint16_t color) {
  int x = slot ? 82 : 6;
  gfx2->fillRoundRect(x, 58, 72, 18, 2, color);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  int16_t bx, by; uint16_t bw, bh;
  gfx2->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  gfx2->setCursor(x + (72 - (int)bw) / 2 - bx, 71);
  gfx2->print(label);
}

// The usual bottom pair: orange Back-style left + colored action right.
// Pass NULL to skip a slot.
void uiButtons(const char *left, const char *right, uint16_t rightColor) {
  if (left)  uiButton(0, left, ORANGE);
  if (right) uiButton(1, right, rightColor);
}

// Small button-like action hint: light rounded square with a dark UP
// triangle inside + a small-font label to the right. Same shape for every
// "press UP does X" hint. x,y = top-left of the 13x13 square.
void uiActionHint(int x, int y, const char *label) {
  gfx2->fillRoundRect(x, y, 13, 13, 3, 0x879F);
  gfx2->fillTriangle(x + 6, y + 3, x + 2, y + 9, x + 10, y + 9, BLACK);
  gfx2->setFont(NULL);              // built-in small font for the label
  gfx2->setTextColor(0x879F);
  gfx2->setCursor(x + 16, y + 3);
  gfx2->print(label);
  gfx2->setTextColor(WHITE);
  gfx2->setFont(&FreeSans8pt7b);
}

// Trim a string by pixel width (in the current font) to fit maxW pixels.
String uiFitText(String s, int maxW) {
  int16_t bx, by; uint16_t bw, bh;
  while (s.length() > 1) {
    gfx2->getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
    if ((int)bw <= maxW) break;
    s.remove(s.length() - 1);
  }
  return s;
}

// Framed two-line status message (used by upload/OTA/import flows)
void netMessage(const char *line1, const char *line2) {
  uiWakeScreen();   // network events must be visible even if the UI timed out
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 32);
  gfx2->print(line1);
  gfx2->setCursor(8, 56);
  gfx2->print(line2);
}

// Two text lines + bounded progress bar (WiFi connect / uploads / OTA / import)
void netProgressStart(const char *line1, const char *line2) {
  uiWakeScreen();   // web update/upload progress must wake a blanked screen
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 18);
  gfx2->print(line1);
  gfx2->setCursor(5, 38);
  gfx2->print(line2);
  gfx2->drawRoundRect(10, 48, 140, 16, 3, WHITE);
}

void netProgressBar(int step, int total) {
  if (total <= 0) return;
  int w = (int)(136L * step / total);
  if (w > 136) w = 136;
  if (w > 0) gfx2->fillRect(12, 50, w, 12, ORANGE);
}

// Rewrites line2 of a netProgressStart() screen, clearing ONLY that row.
//
// This is the whole point: netMessage() called in a loop - which is what
// unpacking used to do - repaints all of 160x80 three times per update, and with
// no double buffer that wipe is visible. Hence the flicker on the unpacking
// screen while the flashing bar, which only ever grows, stays perfectly still.
void netProgressText(const char *line2) {
  gfx2->fillRect(0, 26, 160, 17, BLACK);   // line2's row; line1 sits above at y=18
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 38);
  gfx2->print(line2);
}

// Bar + "done / total" counter. The dashboard tells uploaders to watch the layer
// count on the printer's screen, so the number stays, not just the bar.
void netProgressCount(int done, int total) {
  netProgressBar(done, total);
  char buf[24];
  snprintf(buf, sizeof(buf), "%d / %d", done, total);
  netProgressText(buf);
}

// Boot WiFi-connect indicator: four growing signal bars instead of a progress
// bar (a connect attempt has no meaningful % anyway). Orange bars fill up in a
// cycle while trying; all four turn green on success - same look as the mini
// badge on the main screen, so the state reads consistently.
void netWifiBarsStart(const char *title) {
  uiWakeScreen();
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 18);
  gfx2->print(title);
}

void netWifiBarsPhase(int lit, bool connected) {
  const int bw = 10, gap = 8, base = 72;
  const int x0 = 80 - (4 * bw + 3 * gap) / 2;
  gfx2->fillRect(x0 - 2, base - 42, 4 * bw + 3 * gap + 4, 44, BLACK);
  for (int i = 0; i < 4; i++) {
    int h = 12 + i * 10;                 // 12, 22, 32, 42 px
    int x = x0 + i * (bw + gap);
    bool on = connected || i < lit;
    uint16_t c = connected ? GREEN : ORANGE;
    if (on) gfx2->fillRoundRect(x, base - h, bw, h, 2, c);
    else gfx2->drawRoundRect(x, base - h, bw, h, 2, DARKGREY);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// ---- Boot-animation library (/bootanim/*.tmb) -----------------------------
// The printer keeps a folder of named TMB1 animations; bootAnimName selects the
// active one ("" = built-in splash). Kept out of the ENABLE_NETWORK block so the
// on-device menu + playback work in the network-free build too; the /api/boot-anim
// endpoints and dashboard card live in Network.ino.
#define BOOTANIM_DIR "/bootanim"
#define BOOTANIM_SHUFFLE "__shuffle"

bool bootAnimShuffleSelected(const String &name) {
  return name == BOOTANIM_SHUFFLE;
}

// Keep only [a-z0-9-_], lowercase, <=40 chars; never empty (used as a filename).
String sanitizeAnimName(const String &in) {
  String out;
  for (size_t i = 0; i < in.length() && out.length() < 40; i++) {
    char c = in[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
  }
  return out.length() ? out : String("downloaded");
}

// Fill out[] with the .tmb basenames (no extension) in /bootanim; returns count.
int listBootAnims(String out[], int maxN) {
  int count = 0;
  File dir = SD.open(BOOTANIM_DIR);
  if (!dir) return 0;
  File e;
  while (count < maxN && (e = dir.openNextFile())) {
    char nm[64];
    e.getName(nm, sizeof(nm));
    bool isDir = e.isDirectory();
    e.close();
    if (isDir) continue;
    String s = nm, lower = nm;
    lower.toLowerCase();
    if (!lower.endsWith(".tmb")) continue;
    out[count++] = s.substring(0, s.length() - 4);
  }
  dir.close();
  return count;
}

bool bootAnimExists(const String &name) {
  if (name.length() == 0) return false;
  String path = String(BOOTANIM_DIR) + "/" + name + ".tmb";
  File f = SD.open(path.c_str());
  bool ok = (bool)f;
  if (f) f.close();
  return ok;
}

// "cure-line" -> "Cure Line" for the menu value / dashboard label.
String bootAnimDisplay(const String &name) {
  if (name.length() == 0) return "Default";
  if (bootAnimShuffleSelected(name)) return "Shuffle";
  String out;
  bool up = true;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (c == '-' || c == '_') { out += ' '; up = true; }
    else if (up) { out += (char)toupper(c); up = false; }
    else out += c;
  }
  return out;
}

// Advanced-menu cycle order: Default ("") -> Shuffle (when useful) -> each file -> back to Default.
String nextBootAnim(const String &current) {
  String names[24];
  int n = listBootAnims(names, 24);
  if (current.length() == 0) return n > 1 ? String(BOOTANIM_SHUFFLE) : (n > 0 ? names[0] : String(""));
  if (bootAnimShuffleSelected(current)) return n > 0 ? names[0] : String("");
  for (int i = 0; i < n; i++)
    if (names[i] == current) return (i + 1 < n) ? names[i + 1] : String("");
  return "";  // current was deleted -> back to Default
}

String resolveBootAnimForPlayback(const String &selected) {
  if (!bootAnimShuffleSelected(selected)) return selected;
  String names[24];
  int n = listBootAnims(names, 24);
  if (n < 2) return "";
  return names[esp_random() % n];
}

// Play /bootanim/<name>.tmb on the little screen. Returns false when the file
// is missing or invalid. Shared by the boot splash and the dashboard's "Show"
// preview (Network.ino), which draws over the current screen and restores it.
bool playTmbByName(const String &name) {
  if (name.length() == 0) return false;

  String path = String(BOOTANIM_DIR) + "/" + name + ".tmb";
  File f = SD.open(path.c_str());
  if (!f) return false;

  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12 ||
      hdr[0] != 'T' || hdr[1] != 'M' || hdr[2] != 'B' || hdr[3] != '1') {
    f.close();
    return false;
  }
  uint16_t w   = hdr[4]  | (hdr[5]  << 8);
  uint16_t h   = hdr[6]  | (hdr[7]  << 8);
  uint16_t n   = hdr[8]  | (hdr[9]  << 8);
  uint16_t fps = hdr[10] | (hdr[11] << 8);
  if (w == 0 || w > 160 || h == 0 || h > 80 || n == 0) { f.close(); return false; }

  size_t frameBytes = (size_t)w * h * 2;
  uint16_t *frame = (uint16_t *)malloc(frameBytes);
  if (!frame) { f.close(); return false; }

  int frameDelay = fps > 0 ? (1000 / fps) : 80;
  int16_t ox = (160 - w) / 2, oy = (80 - h) / 2;
  gfx2->fillScreen(BLACK);
  // These files can arrive from a community site, so frameCount (uint16, up to
  // 65535) is untrusted: cap total frames and elapsed time so a corrupt/oversized
  // animation can't hold boot hostage, and let Back skip it immediately.
  const uint16_t MAX_FRAMES = 250;
  const uint32_t MAX_MS     = 10000;
  uint16_t limit = n < MAX_FRAMES ? n : MAX_FRAMES;
  uint32_t animStart = millis();
  // Pace to the header's fps instead of sleeping a full frame on top of the
  // work: reading 25 KB off SD and pushing it over SPI costs ~46 ms, so the
  // old delay(frameDelay) after every frame played every animation ~1.8x
  // slower than authored (malfunction: 3.7 s of frames took 6.3 s). Sleep only
  // the remainder of the frame's slot; if the hardware is slower than fps,
  // skip the sleep rather than fall further behind.
  uint32_t nextDue = millis();
  for (uint16_t i = 0; i < limit; i++) {
    if (f.read((uint8_t *)frame, frameBytes) != (int)frameBytes) break;
    gfx2->draw16bitRGBBitmap(ox, oy, frame, w, h);
    nextDue += frameDelay;
    int32_t slack = (int32_t)(nextDue - millis());
    if (slack > 0) delay(slack);
    if (digitalRead(buttonBack) == LOW) break;   // user skip
    if (millis() - animStart > MAX_MS) break;     // hard time budget
  }
  free(frame);
  f.close();
  return true;
}

// Play the selected boot animation from the /bootanim library. Returns true if
// it played, false to fall back to the built-in splash. Reads the selection
// directly because loadDeviceConfig() runs after screen0() at boot.
bool playBootAnimFromSd() {
  sysPrefs.begin("tinymaker", true);
  String name = sysPrefs.getString("bootAnimName", "");
  sysPrefs.end();
  return playTmbByName(resolveBootAnimForPlayback(name));
}

/**
 * @brief Screen 0: Boot splash
 * Plays the selected animation from the /bootanim library when one is chosen in
 * Advanced; otherwise the built-in rising "Tiny Maker" wordmark.
 */
void screen0(){
  if (playBootAnimFromSd()) return;

  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setCursor(5, 74);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
#ifdef FIRMWARE_VERSION
  gfx2->println(FIRMWARE_VERSION);
#else
  gfx2->println("1.0.2");
#endif
  // 2 px per step, ~0.6 s total - the 1.2 s original dragged the boot out.
  for (int i = 0; i < 40; i += 2) {
    gfx2->setCursor(40, i);
    gfx2->setTextColor(ORANGE);
    gfx2->println("Tiny Maker");
    delay(28);
    gfx2->setCursor(40, i);
    gfx2->setTextColor(BLACK);
    gfx2->println("Tiny Maker");
  }
  gfx2->setCursor(40, 40);
  gfx2->setTextColor(ORANGE);
  gfx2->println("Tiny Maker");
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



// System-menu gear icon, 27x27 px 1-bit bitmap (MSB first, 4 bytes/row).
// Generated from a symmetric radius+angle pixel model: 8 identical teeth,
// rounded cardinal tips, large center hole. Drawn with drawBitmap(ORANGE).
static const uint8_t gearBitmap[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00,
  0x06, 0x1F, 0x0C, 0x00, 0x0F, 0x1F, 0x1E, 0x00, 0x1F, 0xFF, 0xFF, 0x00,
  0x1F, 0xFF, 0xFF, 0x00, 0x0F, 0xFF, 0xFE, 0x00, 0x07, 0xFF, 0xFC, 0x00,
  0x07, 0xE0, 0xFC, 0x00, 0x07, 0xC0, 0x7C, 0x00, 0x3F, 0x80, 0x3F, 0x80,
  0x7F, 0x80, 0x3F, 0xC0, 0x7F, 0x80, 0x3F, 0xC0, 0x7F, 0x80, 0x3F, 0xC0,
  0x3F, 0x80, 0x3F, 0x80, 0x07, 0xC0, 0x7C, 0x00, 0x07, 0xE0, 0xFC, 0x00,
  0x07, 0xFF, 0xFC, 0x00, 0x0F, 0xFF, 0xFE, 0x00, 0x1F, 0xFF, 0xFF, 0x00,
  0x1F, 0xFF, 0xFF, 0x00, 0x0F, 0x1F, 0x1E, 0x00, 0x06, 0x1F, 0x0C, 0x00,
  0x00, 0x1F, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/**
 * @brief Screen 1: Main Menu - Print
 */
void screen1(){
  gfx2->fillScreen(BLACK);

  // Icons centered in their boxes (box interior y15..54, center ~y34.5) to
  // match the gear, and the whole menu sits 2 px lower than the old layout so
  // the WiFi/Connect badge clears the box frames (~4 px gap). Total vertical
  // shift vs the old top-hugging layout: layers/wrench +6, sliders +7, gear +2.
  // Title baseline y73: font descenders (g/y/p = 4 px) reach y77, 2 px clear of
  // the 80 px screen bottom - this is the last comfortable pixel down.
  // Print icon (box 1: x0-40)
  gfx2->fillTriangle(9, 29, 19, 24, 29, 29, ORANGE);
  gfx2->fillTriangle(9, 29, 19, 34, 29, 29, ORANGE);
  gfx2->drawLine(9, 33, 19, 38, ORANGE);
  gfx2->drawLine(9, 37, 19, 42, ORANGE);
  gfx2->drawLine(9, 41, 19, 46, ORANGE);
  gfx2->drawLine(19, 38, 29, 33, ORANGE);
  gfx2->drawLine(19, 42, 29, 37, ORANGE);
  gfx2->drawLine(19, 46, 29, 41, ORANGE);

  // Maintenance icon (box 2: x40-80)
  gfx2->fillCircle(52, 43, 3, ORANGE);
  gfx2->fillCircle(52, 43, 1, BLACK);
  gfx2->fillCircle(65, 30, 6, ORANGE);
  gfx2->fillCircle(67, 28, 3, BLACK);
  gfx2->fillCircle(68, 27, 3, BLACK);
  gfx2->fillTriangle(51, 40, 59, 32, 55, 44, ORANGE);
  gfx2->fillTriangle(55, 44, 63, 36, 59, 32, ORANGE);

  // Settings icon (box 3: x80-120)
  gfx2->fillRoundRect(91, 28, 20, 2, 1, ORANGE);
  gfx2->fillRoundRect(91, 35, 20, 2, 1, ORANGE);
  gfx2->fillRoundRect(91, 42, 20, 2, 1, ORANGE);
  gfx2->fillCircle(104, 28, 2, ORANGE);
  gfx2->fillCircle(99, 35, 2, ORANGE);
  gfx2->fillCircle(104, 42, 2, ORANGE);

  // System icon: gear (box 4: x120-160), drawn from a 1-bit 27x27 bitmap
  // (generated pixel map - 8 identical teeth, rounded tips, big hole;
  // shape approved by the maintainer). See gearBitmap above screen1().
  gfx2->drawBitmap(127, 21, gearBitmap, 27, 27, ORANGE);

  gfx2->drawRoundRect(0, 15, 40, 40, 5, 0x879F);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(63, 73);
  gfx2->print("Print");

  #if ENABLE_NETWORK
  drawWifiBadge();
  #endif
  screen = 1;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 2: Main Menu - Maintenance
 */
void screen2(){
  gfx2->drawRoundRect(0, 15, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(80, 15, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(120, 15, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(40, 15, 40, 40, 5, 0x879F);
  gfx2->fillRect(0, 55, 160, 25, BLACK);
  gfx2->setCursor(31, 73);
  gfx2->print("Maintenance");
  #if ENABLE_NETWORK
  drawWifiBadge();
  #endif
  screen = 2;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 3: Main Menu - Setting
 */
void screen3(){
  gfx2->drawRoundRect(0, 15, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(40, 15, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(120, 15, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(80, 15, 40, 40, 5, 0x879F);
  gfx2->fillRect(0, 55, 160, 25, BLACK);
  gfx2->setCursor(51, 73);
  gfx2->print("Settings");
  #if ENABLE_NETWORK
  drawWifiBadge();
  #endif
  screen = 3;
}
/**
 * @brief Screen 4: Main Menu - System
 */
void screen4(){
  gfx2->drawRoundRect(0, 15, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(40, 15, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(80, 15, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(120, 15, 40, 40, 5, 0x879F);
  gfx2->fillRect(0, 55, 160, 25, BLACK);
  gfx2->setCursor(53, 73);
  gfx2->print("System");
  #if ENABLE_NETWORK
  drawWifiBadge();
  #endif
  screen = 4;
}

// System submenu (0-17b): 5 items, one screen code (41) + a selection index -
// the same pattern as the Advanced groups (440). Update stays visible right
// before About: self-update is the fix-delivery channel, never buried.
#define SYSTEM_MENU_COUNT 5

String systemLabel(int item) {
  if (item == 1) return "WiFi Info";
  if (item == 2) return "Advanced";
  if (item == 3) return "Statistics";
  if (item == 4) return "Update";
  if (item == 5) return "About";
  return "";
}

void drawSystemRow(int item, int y, bool selected) {
  if (item > SYSTEM_MENU_COUNT) item = 1;

  gfx2->drawRoundRect(0, y, 160, 39, 3, selected ? WHITE : BLACK);
  gfx2->setCursor(5, y + 25);
  gfx2->print(systemLabel(item));
}

void drawSystemMenu(uint8_t selected) {
  if (selected < 1) selected = 1;
  if (selected > SYSTEM_MENU_COUNT) selected = SYSTEM_MENU_COUNT;

  int next = selected >= SYSTEM_MENU_COUNT ? 1 : selected + 1;

  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);

  drawSystemRow(selected, 0, true);
  drawSystemRow(next, 41, false);
}

void systemMenuShow() {  // redraw at the current selection (screen stays 41)
  drawSystemMenu(system_item);
  screen = 41;
}

void systemMenuUp() {
  system_item--;
  if (system_item < 1) system_item = SYSTEM_MENU_COUNT;
  systemMenuShow();
}

void systemMenuDown() {
  system_item++;
  if (system_item > SYSTEM_MENU_COUNT) system_item = 1;
  systemMenuShow();
}

// Legacy entry points - callers pick a selection by MEANING (Advanced /
// Update / About), not by position, so reordering the menu can't break them.
void screen41(){ system_item = 1; systemMenuShow(); }   // WiFi Info
void screen42(){ system_item = 2; systemMenuShow(); }   // Advanced
void screen43(){ system_item = 4; systemMenuShow(); }   // Update
void screen44(){ system_item = 5; systemMenuShow(); }   // About

/**
 * @brief Screen 411: shown instead of WiFi Info when ENABLE_NETWORK = 0
 */
void screen411(){
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 35);
  gfx2->print("Network disabled");
  gfx2->setCursor(5, 55);
  gfx2->print("in this build");
  screen = 411;
}

bool advancedMqttConfigured() {
  return mqttHost.length() > 0 || mqttUser.length() > 0 || mqttPass.length() > 0;
}

// WiFi follows the same "ask first, act on OK" pattern as every other
// prompt (since 07-20): nothing changes until Reboot is confirmed. Flipping
// the flag before the prompt looked harmless ("WiFi only changes at boot")
// but network_loop() gates on it - the dashboard died the moment the prompt
// opened (user finding: printer vanished from the browser mid-menu).
void applyWifiToggleAndReboot() {
  wifiEnabled = !wifiEnabled;
  if (!wifiEnabled) {
    webDashboardEnabled = false;
    mqttEnabled = false;
  } else {
    webDashboardEnabled = true;
  }
  saveDeviceConfig();
  ESP.restart();
}

// ---- Advanced menu groups (0-17a) ------------------------------------------
// The flat 10-12 item list became hard to scan on a 2-row screen, so Advanced
// now opens a group list (screen 440): Network / Resin / Display. OK enters
// the group's items (screen 441, the existing mechanics); Back walks up.
// Items keep their ORIGINAL ids (advancedLabel/Value/Select switches are
// untouched) - advancedGroupItem() maps (group, position) to an id, so the
// conditional Network entries (Web control, MQTT) just shorten the group.

int advancedGroupCount() { return 3; }

String advancedGroupLabel(int g) {
  if (g == 1) return "Network";
  if (g == 2) return "Resin";
  if (g == 3) return "Display";
  return "";
}

// Second row of a group card: a one-glance summary of its key state.
String advancedGroupValue(int g) {
  if (g == 1) return wifiEnabled ? "WiFi On" : "WiFi Off";
  if (g == 2) return String(vatRemaining(), 1) + " ml left";
  if (g == 3) {
    if (uiTimeoutSecs == 0) return "Sleep Off";
    return "Sleep " + String(uiTimeoutSecs) + "s";
  }
  return "";
}

int advancedGroupItemCount(int g) {
  if (g == 1) {  // WiFi, [Web control], [MQTT], Boot update
    int count = 2;
    if (wifiEnabled) count++;
    if (wifiEnabled && advancedMqttConfigured()) count++;
    return count;
  }
  if (g == 2) return 6;  // VAT refilled, pause, warn, ask refill, exp test, dry run
  if (g == 3) return 2;  // idle timeout, boot animation
  return 0;
}

// (group, 1-based position) -> original item id used by advancedLabel/Value/
// advancedOptionsSelect. Network keeps WiFi first so the toggles it gates
// (Web control, MQTT) sit right under it.
int advancedGroupItem(int g, int pos) {
  if (g == 1) {
    if (pos == 1) return 7;                                   // WiFi
    if (wifiEnabled && pos == 2) return 11;                   // Web control
    if (wifiEnabled && advancedMqttConfigured() && pos == 3) return 12;  // MQTT
    return 8;                                                 // Boot update (last)
  }
  if (g == 2) {
    const int items[6] = {3, 4, 5, 6, 9, 2};  // refilled, pause, warn, ask, exp test, dry run
    if (pos >= 1 && pos <= 6) return items[pos - 1];
  }
  if (g == 3) {
    if (pos == 1) return 1;   // Idle timeout
    if (pos == 2) return 10;  // Boot animation
  }
  return 1;
}

int advancedOptionCount() { return advancedGroupItemCount(advanced_group); }

String advancedLabel(int item) {
  if (item == 1) return "Idle timeout";   // sleeps only when idle - the old
                                          // "Screen timeout" read as if the
                                          // screen should sleep mid-print too
  if (item == 2) return "Dry run";
  if (item == 3) return "VAT refilled";
  if (item == 4) return "Low resin pause";
  if (item == 5) return "Low resin warn";
  if (item == 6) return "Ask refill";
  if (item == 7) return "WiFi";
  if (item == 8) return "Boot update";
  if (item == 9) return "Exposure test";
  if (item == 10) return "Boot animation";
  if (item == 11) return "Web control";  // shown only via the Network group,
  if (item == 12) return "MQTT";         // which gates them on wifiEnabled
  return "";
}

String advancedValue(int item) {
  if (item == 1) {
    if (uiTimeoutSecs == 0) return "Off";
    return String(uiTimeoutSecs) + "s";
  }
  if (item == 2) return uvLedEnabled ? "Off" : "On";
  if (item == 3) return String(vatRemaining(), 1) + " ml left";
  if (item == 4) return lowResinPauseEnabled ? "On" : "Off";
  if (item == 5) return String(lowResinThresholdMl) + " ml";
  if (item == 6) return askRefillEnabled ? "On" : "Off";
  if (item == 7) return wifiEnabled ? "On" : "Off";
  if (item == 8) return bootUpdateCheckEnabled ? "On" : "Off";
  if (item == 9) return String(expTestBarSecs(1)) + "-" + String(expTestBarSecs(8)) + "s strip";
  if (item == 10) {
    if (bootAnimName.length() == 0) return "Default";
    if (bootAnimShuffleSelected(bootAnimName)) return "Shuffle";
    return bootAnimExists(bootAnimName) ? bootAnimDisplay(bootAnimName) : bootAnimDisplay(bootAnimName) + " (missing)";
  }
  if (item == 11) return webDashboardEnabled ? "On" : "Off";
  if (item == 12) return mqttEnabled ? "On" : "Off";
  return "";
}

void drawAdvancedRow(int pos, int y, bool selected) {
  if (pos > advancedOptionCount()) pos = 1;
  int id = advancedGroupItem(advanced_group, pos);
  gfx2->drawRoundRect(0, y, 160, 39, 3, selected ? WHITE : BLACK);
  gfx2->setCursor(5, y + 15);
  gfx2->print(advancedLabel(id));
  gfx2->setCursor(5, y + 33);
  gfx2->print(advancedValue(id));
}

void screenAdvancedOptions() {
  int count = advancedOptionCount();
  if (advanced_item < 1) advanced_item = 1;
  if (advanced_item > count) advanced_item = count;
  int next = advanced_item >= count ? 1 : advanced_item + 1;
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  drawAdvancedRow(advanced_item, 0, true);
  drawAdvancedRow(next, 41, false);
  screen = 441;
}

// Group list (screen 440) - same 2-row mechanics as the item list above.
void drawAdvancedGroupRow(int g, int y, bool selected) {
  if (g > advancedGroupCount()) g = 1;
  gfx2->drawRoundRect(0, y, 160, 39, 3, selected ? WHITE : BLACK);
  gfx2->setCursor(5, y + 15);
  gfx2->print(advancedGroupLabel(g));
  gfx2->setCursor(5, y + 33);
  gfx2->print(advancedGroupValue(g));
}

void screenAdvancedGroups() {
  int count = advancedGroupCount();
  if (advanced_group < 1) advanced_group = 1;
  if (advanced_group > count) advanced_group = count;
  int next = advanced_group >= count ? 1 : advanced_group + 1;
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  drawAdvancedGroupRow(advanced_group, 0, true);
  drawAdvancedGroupRow(next, 41, false);
  screen = 440;
}

void advancedGroupsUp() {
  advanced_group--;
  if (advanced_group < 1) advanced_group = advancedGroupCount();
  screenAdvancedGroups();
}

void advancedGroupsDown() {
  advanced_group++;
  if (advanced_group > advancedGroupCount()) advanced_group = 1;
  screenAdvancedGroups();
}

void advancedOptionsUp() {
  advanced_item--;
  if (advanced_item < 1) advanced_item = advancedOptionCount();
  screenAdvancedOptions();
}

void advancedOptionsDown() {
  advanced_item++;
  if (advanced_item > advancedOptionCount()) advanced_item = 1;
  screenAdvancedOptions();
}

void advancedOptionsSelect() {
  // The visible position resolves to the item's original id - the switch
  // below stayed keyed by id when the menu grew groups (0-17a).
  int id = advancedGroupItem(advanced_group, advanced_item);
  if (id == 1) {
    if (uiTimeoutSecs == 0) uiTimeoutSecs = 30;
    else if (uiTimeoutSecs < 60) uiTimeoutSecs = 60;
    else if (uiTimeoutSecs < 120) uiTimeoutSecs = 120;
    else if (uiTimeoutSecs < 300) uiTimeoutSecs = 300;
    else if (uiTimeoutSecs < 600) uiTimeoutSecs = 600;
    else uiTimeoutSecs = 0;
  } else if (id == 2) {
    uvLedEnabled = !uvLedEnabled;
  } else if (id == 3) {
    vatMarkRefilled();          // action item: bookkeeping restarts from full
  } else if (id == 4) {
    lowResinPauseEnabled = !lowResinPauseEnabled;
  } else if (id == 5) {
    lowResinThresholdMl++;      // cycle 1..3 ml
    if (lowResinThresholdMl > 3) lowResinThresholdMl = 1;
  } else if (id == 6) {
    askRefillEnabled = !askRefillEnabled;
  } else if (id == 7) {
    // No mutation here - the prompt asks first, and the toggle is applied
    // only on the confirmed reboot (applyWifiToggleAndReboot).
    screenRebootConfirm();
    return;
  } else if (id == 8) {
    bootUpdateCheckEnabled = !bootUpdateCheckEnabled;
  } else if (id == 9) {
    screenExpTestIntro();       // action item: opens the exposure test flow
    return;
  } else if (id == 10) {
    bootAnimName = nextBootAnim(bootAnimName);  // cycle Default -> each installed animation
  } else if (id == 11) {
    webDashboardEnabled = !webDashboardEnabled;
  } else if (id == 12) {
    mqttEnabled = !mqttEnabled;
  }
  saveDeviceConfig();
  #if ENABLE_NETWORK
  tinymakerConnectScheduleBackup();
  #endif
  screenAdvancedOptions();
}

/**
 * @brief Screen 113: low-resin warning before starting a print.
 * needMl >= 0 when a fresh model estimate exists (Start from the resin
 * screen); -1 = threshold-only warning. OK starts anyway, Back cancels.
 */
void screenLowResinWarn(float needMl) {
  uiFrame(RED);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 21);
  gfx2->print("Low resin!");
  uiActionHint(92, 8, "Refilled");   // UP = mark full & start (top-right)
  gfx2->setTextColor(0x879F);
  gfx2->setCursor(8, 43);
  if (needMl >= 0) {
    gfx2->print("Need ");
    gfx2->print(needMl, 1);
    gfx2->print(", have ~");
    gfx2->print(vatRemaining(), 1);
  } else {
    gfx2->print("~");
    gfx2->print(vatRemaining(), 1);
    gfx2->print(" ml left in VAT");
  }
  gfx2->setTextColor(WHITE);
  uiButtons("Back", "Start", 0x879F);
  screen = 114;
}

/**
 * @brief Screen 115: "VAT refilled?" ask before every print (optional).
 * OK = yes (bookkeeping restarts from a full VAT), Back = no, start
 * with the current estimate. Disable under System > Advanced.
 */
void screenRefillAsk() {
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 21);
  gfx2->print("VAT refilled?");
  gfx2->setTextColor(0x879F);
  gfx2->setCursor(8, 43);
  gfx2->print("~");
  gfx2->print(vatRemaining(), 1);
  gfx2->print(" ml left now");
  gfx2->setTextColor(WHITE);
  uiButtons("No", "Yes", 0x879F);
  screen = 115;
}

/**
 * @brief Screen 442: WiFi toggle confirmation. Says WHAT will change (user
 * finding 07-20: "Reboot required" alone didn't). Nothing has been touched
 * yet - wifiEnabled still holds the CURRENT value, so the prompt offers its
 * opposite. OK applies the toggle and reboots; Back leaves everything as is.
 */
void screenRebootConfirm() {
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 21);
  gfx2->print(wifiEnabled ? "Turn WiFi Off?" : "Turn WiFi On?");
  gfx2->setCursor(8, 43);
  gfx2->print("Reboots to apply");
  uiButtons("Cancel", "Reboot", 0x879F);
  screen = 442;
}

#if ENABLE_NETWORK
/**
 * @brief Draw the Update screen's bottom row.
 * Left  "Local" = UP opens the "install from file" screen (422).
 * Right "Install" = OK self-updates; blue when an update is available,
 * greyed out otherwise. (Physical Back leaves - see loop().)
 */
void screen421Buttons(bool installActive){
  // Left: UP -> install from file (browser upload of any/older version)
  uiActionHint(5, 60, "Local");
  // Right: OK -> install latest (self-update); greyed when nothing newer
  uiButton(1, "Install", installActive ? 0x879F : DARKGREY);
  gfx2->setFont(NULL);
}

/**
 * @brief Dim idle screen shown after the UI timeout instead of a black
 * displayOff(). The backlight is hard-wired on, so a blanked screen still
 * glows - a low-key status (wordmark + IP) makes the printer look alive and
 * keeps the dashboard URL in sight, at the same power draw. Static by design:
 * drawn once when idling starts (handleUiTimeout latches uiBlanked), redrawn
 * as the normal UI on the next key/network wake.
 */
// 0-21/0-22 screen saver core: the drifting dim block at one of 5 spots
// (4 corners + centre). No wordmark (V 07-21) - the state IS the message:
// line 1 big = state ("Idle"/"Printing"), line 2 big = live numbers
// ("47% 1h23m", printing only), last line small = the IP so the dashboard
// stays reachable.
static void drawSaverBlock(uint8_t pos, const char *stateBig, const String &numsBig){
  const int bw = 120, W = 160, H = 80, m = 4;
  const bool nums = numsBig.length() > 0;
  const int bh = nums ? 48 : 30;
  int x = (pos == 1 || pos == 3) ? (W - bw - m) : (pos == 4) ? (W - bw) / 2 : m;
  int y = (pos == 2 || pos == 3) ? (H - bh - m) : (pos == 4) ? (H - bh) / 2 : m;
  gfx2->fillScreen(BLACK);
  gfx2->setFont(NULL);
  gfx2->setTextSize(2);
  gfx2->setTextColor(0x4208);            // dim grey headline
  gfx2->setCursor(x, y);
  gfx2->print(stateBig);
  if (nums) {
    gfx2->setCursor(x, y + 18);
    gfx2->print(numsBig);
  }
  gfx2->setTextSize(1);
  gfx2->setTextColor(0x2124);            // dimmer IP line
  if (WiFi.status() == WL_CONNECTED) {
    gfx2->setCursor(x, y + (nums ? 40 : 22));
    gfx2->print(WiFi.localIP());
  }
  gfx2->setTextSize(1);
  gfx2->setFont(&FreeSans8pt7b);
}

void drawIdleScreen(uint8_t pos){
  drawSaverBlock(pos, "Idle", "");
}

// 0-22: printing saver - "Printing" + "47% 1h23m" (percent + time left),
// both in the big font so the state reads across the room.
void drawPrintSaver(uint8_t pos){
  String s = "";
  if (layer_counter > 0) {
    s += String((int)((long)current_layer * 100L / layer_counter));
    s += "% ";
  }
  uint32_t rem = remainingPrintSecs();
  if (rem > 0) {
    if (rem >= 3600) { s += String(rem / 3600); s += "h"; }
    s += String((rem % 3600) / 60);
    s += "m";
  }
  drawSaverBlock(pos, "Printing", s);
}

/**
 * @brief Screen 422: Install from file (browser upload of a specific/older
 * version). Reached with UP from the Update screen; Back returns to 421.
 */
void screen422(){
  gfx2->fillScreen(BLACK);
  uiTitle("Install from file");
  gfx2->setFont(NULL);
  gfx2->setTextColor(WHITE);
  gfx2->setCursor(5, 30);
  gfx2->print("Open in a browser and");
  gfx2->setCursor(5, 42);
  gfx2->print("upload a firmware.bin:");
  gfx2->setCursor(5, 58);
  if (WiFi.status() == WL_CONNECTED) {
    gfx2->print("URL: ");
    gfx2->print(WiFi.localIP());
    gfx2->print("/update");
  } else {
    gfx2->print("WiFi not connected");
  }
  gfx2->setFont(&FreeSans8pt7b);
  screen = 422;
}

void screenUpdateWifiConfirm(){
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 21);
  gfx2->print("WiFi is off");
  gfx2->setCursor(8, 43);
  gfx2->print("Enable for update?");
  uiButtons("No", "Yes", 0x879F);
  screen = 423;
}

void screenBootUpdatePrompt(){
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 21);
  gfx2->print("Update available");
  gfx2->setCursor(8, 43);
  gfx2->print("v");
  gfx2->print(otaLatestVerStr());
  uiButtons("Later", "Install", 0x879F);
  screen = 424;
}

void screenBootUpdateDisablePrompt(){
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 21);
  gfx2->print("Disable boot");
  gfx2->setCursor(8, 43);
  gfx2->print("update check?");
  uiButtons("No", "Yes", 0x879F);
  screen = 425;
}
#endif

/**
 * Screen 426: offer to restore settings from the SD backup right after a
 * factory-fresh boot (full USB reflash wiped EEPROM+NVS; the SD survives).
 * Compiled unconditionally - restore must work in the network-free build too.
 */
void screenRestorePrompt(){
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 21);
  gfx2->print("Restore settings");
  gfx2->setCursor(8, 43);
  gfx2->print("from SD backup?");
  uiButtons("Skip", "Restore", 0x879F);
  screen = 426;
}

void screenRestoreDone(bool ok){
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 34);
  gfx2->print(ok ? "Settings restored" : "Restore failed");
  delay(1200);
}

/**
 * Screen 427: power was lost mid-print and a valid checkpoint is on the SD
 * card (resumeLoad() in Resume.ino). OK resumes at the recorded layer -
 * without homing - Back discards the checkpoint and boots normally.
 */
void screenResumePrompt(){
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 18);
  gfx2->print("Resume lost print?");
  gfx2->setTextColor(0x879F);
  gfx2->setCursor(8, 38);
  gfx2->print(uiFitText(String(resumeFolder), 144));
  gfx2->setCursor(8, 56);
  gfx2->print("Layer ");
  gfx2->print(resumeLayer);
  gfx2->print(" / ");
  gfx2->print(resumeTotal);
  gfx2->setTextColor(WHITE);
  uiButtons("Discard", "Resume", 0x879F);
  screen = 427;
}

/**
 * @brief Screen 421: Firmware update - installed vs latest, self-update.
 * Shows the installed version, checks GitHub Pages for the latest, and
 * offers "Install" (self-update, no PC). UP ("Local") opens screen 422 to
 * install a specific/older version from a file via the browser.
 */
// Screen 4211: confirmation before the on-device self-update Install -
// guards against an accidental OK on the Update screen (the dashboard
// already confirms downgrades; the LCD Install used to flash immediately).
void screenUpdateConfirm(){
#if ENABLE_NETWORK
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 18);
  gfx2->print("Install update?");
  gfx2->setTextColor(0x879F);
  gfx2->setCursor(8, 38);
  // "old -> new" reads in the direction it happens and, unlike the old
  // "vNEW (now vOLD)", it still fits the 160 px line at three-digit versions.
#ifdef FIRMWARE_VERSION
  gfx2->print(String(FIRMWARE_VERSION) + " -> ");
#endif
  gfx2->print(otaLatestVerStr());
  gfx2->setCursor(8, 52);
  gfx2->print("Installs and reboots");
  gfx2->setTextColor(WHITE);
  uiButtons("Cancel", "Install", 0x879F);
  screen = 4211;
#endif
}

void screen421(){
  gfx2->fillScreen(BLACK);
  uiTitle("Firmware update");
#if ENABLE_NETWORK
  gfx2->setFont(NULL); // built-in small font for versions
  gfx2->setTextColor(WHITE);
  gfx2->setCursor(5, 26);
  gfx2->print("Installed: v");
#ifdef FIRMWARE_VERSION
  gfx2->print(FIRMWARE_VERSION);
#else
  gfx2->print("?");
#endif

  if (WiFi.status() != WL_CONNECTED) {
    gfx2->setCursor(5, 42);
    gfx2->print("WiFi not connected");
    gfx2->setFont(&FreeSans8pt7b);
    screen = 421;
    return;
  }

  // First paint: "checking...", Local hint + greyed Install
  gfx2->setCursor(5, 40);
  gfx2->print("Latest: checking...");
  screen421Buttons(false);
  screen = 421;

  // Blocking HTTPS check, then refresh the Latest line + Install button
  otaCheckLatest();
  gfx2->fillRect(0, 36, 160, 12, BLACK);   // clear the Latest line (glyphs span y40..47)
  gfx2->setFont(NULL);
  gfx2->setCursor(5, 40);
  int st = otaVersionState();
  if (st == 3) {                            // newer available
    gfx2->setTextColor(0x879F);
    gfx2->print("Latest: v");
    gfx2->print(otaLatestVerStr());
    screen421Buttons(true);
  } else if (st == 2) {                     // already current
    gfx2->setTextColor(WHITE);
    gfx2->print("Up to date");
    screen421Buttons(false);
  } else {                                  // error / offline
    gfx2->setTextColor(WHITE);
    gfx2->print("Latest: unknown");
    screen421Buttons(false);
  }
  gfx2->setFont(&FreeSans8pt7b); // restore UI font
#else
  gfx2->setTextColor(ORANGE);
  gfx2->setCursor(5, 55);
  gfx2->print("Network disabled");
  screen = 421;
#endif
}

/**
 * @brief Screen 431: About
 */
void screen431(){
  gfx2->fillScreen(BLACK);
  uiTitle("TinyMaker WiFi");
  gfx2->setFont(NULL); // built-in small font for long lines
  gfx2->setTextColor(WHITE);
  // Pure identity info (0-17b) - the counters moved to Statistics (432),
  // matching the dashboard's Settings > About / Statistics split.
  gfx2->setCursor(5, 24);
  gfx2->setTextColor(0x879F);
  gfx2->print("FW: ");
  gfx2->setTextColor(WHITE);
#ifdef FIRMWARE_VERSION
  gfx2->print("v");
  gfx2->print(FIRMWARE_VERSION);
#else
  gfx2->print("v?");
#endif
  gfx2->setCursor(5, 35);
  gfx2->print("Based on TinyMaker 1.0.2");
  gfx2->setCursor(5, 46);
  gfx2->setTextColor(0x879F);
  gfx2->print("Built: ");
  gfx2->setTextColor(WHITE);
  gfx2->print(__DATE__);
  // Classic-font setCursor() y is the glyph TOP (7 px tall): the last row
  // must start by y=73 or it clips past the 80 px panel edge.
  gfx2->setCursor(5, 64);
  gfx2->print("github.com/slibbinas/");
  gfx2->setCursor(5, 73);
  gfx2->print("TinyMakerWifi");
  gfx2->setFont(&FreeSans8pt7b); // restore UI font
  screen = 431;
}

/**
 * @brief Screen 432: Statistics (0-17b) - counters + reset-reason telemetry.
 * The printer-side face of 0-30: "Boot:" is this boot's reason, "Crash:" the
 * last recorded mid-print death (reason shortened to fit 26 small-font cols).
 */
void screen432(){
  gfx2->fillScreen(BLACK);
  uiTitle("Statistics");
  gfx2->setFont(NULL);
  gfx2->setTextColor(WHITE);
  gfx2->setCursor(5, 24);
  gfx2->setTextColor(0x879F);
  gfx2->print("Printed: ");
  gfx2->setTextColor(WHITE);
  gfx2->print(totalPrintSecs / 3600UL);
  gfx2->print("h ");
  gfx2->print((totalPrintSecs % 3600UL) / 60UL);
  gfx2->print("m");
  gfx2->setCursor(5, 35);
  gfx2->setTextColor(0x879F);
  gfx2->print("UV LED: ");
  gfx2->setTextColor(WHITE);
  gfx2->print(totalUvLedSecs / 3600UL);
  gfx2->print("h ");
  gfx2->print((totalUvLedSecs % 3600UL) / 60UL);
  gfx2->print("m");
  gfx2->setCursor(5, 46);
  gfx2->setTextColor(0x879F);
  gfx2->print("Boot: ");
  gfx2->setTextColor(WHITE);
  gfx2->print(resetReasonName((uint8_t)bootResetReason));
  gfx2->setCursor(5, 57);
  gfx2->setTextColor(0x879F);
  gfx2->print("Crash: ");
  gfx2->setTextColor(WHITE);
  if (crashSeen) {
    String r = resetReasonName(crashReason);
    int paren = r.indexOf(" (");           // "brownout (power dip)" -> "brownout"
    if (paren > 0) r = r.substring(0, paren);
    gfx2->print(r);
    gfx2->setCursor(5, 68);
    gfx2->print("  at layer ");
    gfx2->print(crashLayer);
  } else {
    gfx2->print("none recorded");
  }
  gfx2->setFont(&FreeSans8pt7b);
  screen = 432;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 11: File Selection
 * Displays a list of files/folders.
 */
void screen11(){
  uiFrame(ORANGE);
  gfx2->fillRoundRect(0, 0, 160, 20, 3, ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(43, 14);
  gfx2->print("Select File");
  gfx2->drawRoundRect(6, 27, 148, 24, 3, WHITE);
  gfx2->fillTriangle(147, 32, 144, 35, 150, 35, WHITE);
  gfx2->fillTriangle(147, 45, 144, 42, 150, 42, WHITE);
  uiButtons("Back", "Next", 0x879F);
  screen = 11;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 111: File Preview
 * Calculates print parameters and displays them.
 */
void screen111(){ 
  layer_counter = 0;
  total_height = 0;
  estimated_seconds = 0;
  estimated_hours = 0;
  estimated_minutes = 0;
  motor_updown_time = 0;
  motor_updown_time_total = 0;
  
  File entry;
  // Fast seek to avoid slow iterating over all files for layer counting
  do {
    layer_counter += 100;
    FileName = foldersel_long;
    FileName += "/";
    FileName += layer_counter;
    FileName += ".png";
    entry = SD.open(FileName);        
  } while(entry);
  layer_counter -= 100;

  // Exact seek
  do {
    layer_counter++;
    FileName = foldersel_long;
    FileName += "/";
    FileName += layer_counter;
    FileName += ".png";
    entry = SD.open(FileName);    
  } while(entry);
  layer_counter --;

  // Calculate motor timing  
  get_motor_updown_time();

  // Calculate height
  if(Layer_Height < 0.09)
    total_height = layer_counter * 0.05; 
  if(Layer_Height > 0.06){
    layer_counter /= 2;
    total_height = 0.1 * layer_counter;  
  }

  // Calculate print time
  estimated_seconds += Base_Layer * Base_Exposure;
  estimated_seconds += (layer_counter - Base_Layer) * Regular_Exposure;
  motor_updown_time_total = motor_updown_time * (layer_counter - 1);
  estimated_seconds += motor_updown_time_total; 
  estimated_hours = estimated_seconds / 3600;
  estimated_minutes = (estimated_seconds % 3600) / 60; 

  // Draw UI
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(7, 16);
  gfx2->print("Layers: ");
  gfx2->print(layer_counter);
  uiActionHint(118, 5, "ml");   // UP on this screen estimates resin
  gfx2->setCursor(7, 34);
  gfx2->print("Height: ");
  gfx2->print(total_height);
  gfx2->print("mm");
  gfx2->setCursor(7, 52);
  gfx2->print("Time: ");
  gfx2->print(estimated_hours);
  gfx2->print("h ");
  gfx2->print(estimated_minutes);
  gfx2->print("min");
  uiButtons("Back", "Start", 0x879F);
  screen = 111;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 112: Height Warning
 * Displays warning if object height exceeds build volume.
 */
void screen112(){
  uiFrame(RED);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED);
  gfx2->fillCircle(11, 18, 2, RED);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(20, 16);
  gfx2->println("The height of this");
  gfx2->setCursor(6, 34);
  gfx2->println("object exceeds the");
  gfx2->setCursor(6, 52);
  gfx2->println("max build volume.");
  uiButton(0, "Back", ORANGE);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  screen = 112;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 1111: Printing Status
 * Shows current print progress including layer, time, and file name.
 */
void screen1111(){
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 120, 80, 5, ORANGE);
  gfx2->fillRoundRect(2, 2, 116, 76, 3, BLACK);
  gfx2->fillRoundRect(0, 0, 120, 20, 3, ORANGE);    
    
  // Icons (Pause/Cancel)    
  gfx2->fillRect(136, 12, 16, 16, RED);
  gfx2->fillRect(136, 52, 6, 16, YELLOW);
  gfx2->fillRect(146, 52, 6, 16, YELLOW);
  
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(6, 34);
  gfx2->setTextColor(WHITE);
  gfx2->print(uiFitText(foldersel, 106));   // narrower box than the file list
  gfx2->setCursor(6, 54);
  gfx2->print(current_layer);
  gfx2->print(" / ");
  gfx2->print(layer_counter);
  gfx2->setCursor(6, 74);
  gfx2->print(estimated_hours);
  gfx2->print("h");
  gfx2->print(estimated_minutes);
  gfx2->print("m ");
  gfx2->setTextColor(0x879F);
  gfx2->print(resinUsedMl, 1);
  gfx2->print("ml");
  gfx2->setTextColor(WHITE);
  screen = 1111;  
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 1111 State: Update Print State
 * Updates the top status bar text based on `current_state`.
 */
void screen1111_state(){
  // 0-22: while dimmed, curing/lifting/dropping updates stay off the saver -
  // but a cancel/pause/finish (state >= 4) must wake the screen and show.
  if (uiDimmedPrint) {
    if (current_state >= 4) {
      uiDimmedPrint = false;
      screen1111();
    } else {
      return;
    }
  }
  if (screen != 11111 && screen != 11112){
    gfx2->fillRoundRect(0, 0, 120, 20, 3, ORANGE);    
    gfx2->setFont(&FreeSans8pt7b);
    gfx2->setTextColor(WHITE);
    gfx2->setTextSize(1);
    switch (current_state){
      case 0:
      gfx2->setCursor(33, 14);
      gfx2->print("Homing...");
        break;
      case 1:
      gfx2->setCursor(22, 14);
      gfx2->print("UV Curing...");
        break;
      case 2:
      gfx2->setCursor(37, 14);
      gfx2->print("Lifting...");
        break;
      case 3:
      gfx2->setCursor(26, 14);
      gfx2->print("Dropping...");
        break;
      case 4:
      gfx2->setCursor(24, 14);
      gfx2->print("Canceling...");
        break;
      case 5:
      gfx2->setCursor(30, 14);
      gfx2->print("Pausing...");
        break;  
      case 6:
      gfx2->setCursor(32, 14);
      gfx2->print("Paused");
        break;
      case 7:
      gfx2->setCursor(24, 14);
      gfx2->print("Resuming...");
        break;
      case 8:
      gfx2->setCursor(32, 14);
      gfx2->print("Finish :)");
        break;
      case 10:                  // low-resin pause (paused variant)
      gfx2->setCursor(26, 14);
      gfx2->print("Refill VAT!");
        break;
    }
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 1112: Draw Resume Icon
 * Draws the play (triangle) icon to indicate resuming.
 */
void screen1112(){
  gfx2->fillRect(136, 52, 6, 16, BLACK);
  gfx2->fillRect(146, 52, 6, 16, BLACK);
  gfx2->fillTriangle(136, 52, 136, 68, 152, 60, GREEN);
  screen = 1112;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 1111UP: Highlight Cancel
 * Highlights the Cancel button.
 */
void screen1111UP(){
  gfx2->drawRoundRect(128, 4, 32, 32, 3, WHITE);
  gfx2->drawRoundRect(128, 44, 32, 32, 3, BLACK);
  printing_item_updown = 1;  
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 1111DOWN: Highlight Pause
 * Highlights the Pause button.
 */
void screen1111DOWN(){
  gfx2->drawRoundRect(128, 4, 32, 32, 3, BLACK);
  gfx2->drawRoundRect(128, 44, 32, 32, 3, WHITE);
  printing_item_updown = 0;  
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 11111: Cancel Confirmation
 * Asks user if they are sure to cancel.
 */
void screen11111(){
  gfx2->fillRoundRect(5, 5, 150, 70, 7, BLACK);
  gfx2->fillRoundRect(7, 7, 146, 66, 5, RED);
  gfx2->fillRoundRect(9, 9, 142, 62, 3, BLACK);
  gfx2->fillRoundRect(16, 11, 5, 10, 1, RED); 
  gfx2->fillCircle(18, 25, 2, RED); 
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(27, 23);
  gfx2->println("Are you sure to");
  gfx2->setCursor(13, 41);
  gfx2->println("cancel the print?"); 
  gfx2->fillRoundRect(11, 51, 67, 18, 2, ORANGE);
  gfx2->setCursor(27, 64);
  gfx2->println("Back");
  gfx2->fillRoundRect(82, 51, 67, 18, 2,  0x879F);
  gfx2->setCursor(100, 64);
  gfx2->println("Sure");
  screen = 11111;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 11112: Pause Confirmation
 * Asks user if they are sure to pause.
 */
void screen11112(){
  gfx2->fillRoundRect(5, 5, 150, 70, 7, BLACK);
  gfx2->fillRoundRect(7, 7, 146, 66, 5, RED);
  gfx2->fillRoundRect(9, 9, 142, 62, 3, BLACK);
  gfx2->fillRoundRect(16, 11, 5, 10, 1, RED); 
  gfx2->fillCircle(18, 25, 2, RED); 
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(27, 23);
  gfx2->println("Are you sure to");
  gfx2->setCursor(13, 41);
  gfx2->println("pause the print?"); 
  gfx2->fillRoundRect(11, 51, 67, 18, 2, ORANGE);
  gfx2->setCursor(27, 64);
  gfx2->println("Back");
  gfx2->fillRoundRect(82, 51, 67, 18, 2,  0x879F);
  gfx2->setCursor(100, 64);
  gfx2->println("Sure");
  screen = 11112;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 11113: Resume Confirmation
 * Asks user if they are sure to resume.
 */
void screen11113(){
  gfx2->fillRoundRect(5, 5, 150, 70, 7, BLACK);
  gfx2->fillRoundRect(7, 7, 146, 66, 5, RED);
  gfx2->fillRoundRect(9, 9, 142, 62, 3, BLACK);
  gfx2->fillRoundRect(16, 11, 5, 10, 1, RED); 
  gfx2->fillCircle(18, 25, 2, RED); 
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(27, 23);
  gfx2->println("Are you sure to");
  gfx2->setCursor(13, 41);
  gfx2->println("resume the print?"); 
  gfx2->fillRoundRect(11, 51, 67, 18, 2, ORANGE);
  gfx2->setCursor(27, 64);
  gfx2->println("Back");
  gfx2->fillRoundRect(82, 51, 67, 18, 2,  0x879F);
  gfx2->setCursor(100, 64);
  gfx2->println("Sure");
  screen = 11113;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 12: SD Card Error
 * Displays error when SD card is invalid or missing.
 */
void screen12(){
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, RED);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED); 
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(20, 16);
  gfx2->println("Please insert a");
  gfx2->setCursor(6, 34);
  gfx2->println("supported SD Card");
  gfx2->fillRoundRect(6, 58, 72, 18, 2, ORANGE);
  gfx2->setCursor(24, 71);
  gfx2->println("Back");
  gfx2->fillRoundRect(82, 58, 72, 18, 2,  0x879F);
  gfx2->setCursor(92, 71);
  gfx2->println("Refresh");
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  screen = 12;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 21: Maintenance Sub-menu
 * Options for Leveling, Move, Clean.
 */
void screen21(){
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setCursor(7, 17);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->println("Level Build Plate");
  gfx2->setCursor(7, 44);
  gfx2->print("Move Build Plate");
  gfx2->setCursor(7, 71);
  gfx2->print("Clean Resin Vat");
  gfx2->drawRoundRect(1, 1, 158, 24, 3, WHITE);
  screen = 21;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 22: Move Build Plate Highlight
 */
void screen22(){
  gfx2->drawRoundRect(1, 1, 158, 24, 3, BLACK);
  gfx2->drawRoundRect(1, 28, 158, 24, 3, WHITE);
  gfx2->drawRoundRect(1, 55, 158, 24, 3, BLACK);
  screen = 22;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 23: Clean Resin Vat Highlight
 */
void screen23(){
  gfx2->drawRoundRect(1, 1, 158, 24, 3, BLACK);
  gfx2->drawRoundRect(1, 28, 158, 24, 3, BLACK);
  gfx2->drawRoundRect(1, 55, 158, 24, 3, WHITE);
  screen = 23;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 211: Calibration Warning
 * Advises user not to re-level unless necessary.
 */
void screen211(){
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, RED);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);

  //Exclamation Mark  
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED); 
  
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  // Safety first: leveling homes DOWNWARD - with a print still on the plate it
  // would press it into the FEP/LCD. A user recovering from a power outage lands
  // exactly here in a panic (field incident: feedback #8, 2026-07-17). The
  // factory-calibration nuance stays as the closing line.
  // Three SHORT lines: five trailing caps made "...EMPTY" overflow the frame
  // and wrap onto the next row, colliding with line 2 (field photo, 07-20).
  // Caps are ~10 px vs ~7 px lowercase in FreeSans8pt - budget them as such.
  gfx2->setCursor(20, 16);
  gfx2->println("Plate must be");
  gfx2->setCursor(6, 34);
  gfx2->println("EMPTY. Homing");
  gfx2->setCursor(6, 52);
  gfx2->println("goes DOWN.");
  
  gfx2->fillRoundRect(6, 58, 72, 18, 2, ORANGE);
  gfx2->setCursor(24, 71);
  gfx2->println("Back");
  gfx2->fillRoundRect(82, 58, 72, 18, 2,  0x879F);
  gfx2->setCursor(102, 71);
  gfx2->println("Next");

  // Animation Loop  
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  screen = 211;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 212: Instruction Warning
 * Asks user to read instructions before proceeding.
 */
void screen212(){
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, RED);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED); 
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(20, 16);
  gfx2->println("Please read the");
  gfx2->setCursor(6, 34);
  gfx2->println("instruction carefully");
  gfx2->setCursor(6, 52);
  gfx2->println("before doing this.");
  gfx2->fillRoundRect(6, 58, 72, 18, 2, ORANGE);
  gfx2->setCursor(24, 71);
  gfx2->println("Back");
  gfx2->fillRoundRect(82, 58, 72, 18, 2,  0x879F);
  gfx2->setCursor(102, 71);
  gfx2->println("Start");
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  screen = 212;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 213: Homing Z Axis
 * Moves the Z-axis to the home position (endstop).
 */
void screen213(){
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, ORANGE);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);
  gfx2->fillRoundRect(0, 0, 160, 20, 3, ORANGE);  
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(30, 14);
  gfx2->print("Homing Z axis");
  gfx2->setCursor(36, 43);
  gfx2->print("Please wait...");
  gfx2->fillRoundRect(6, 58, 72, 18, 2, ORANGE);
  gfx2->setCursor(16, 71);
  gfx2->println("Cancel");
  screen = 213;

  // Homing Motion
  stepper.setCurrentPosition(0);
  stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
  stepper.enableOutputs();
  long initial_homing = 0;
  long current_position;
  while(!digitalRead(end_stop)){
    stepper.moveTo(initial_homing);  // Set the position to move to
    initial_homing--;  // Decrease by 1 for next move if needed
    stepper.run();  // Start moving the stepper 
    current_position = stepper.currentPosition();
    if (current_position < -106799){
      stepper.disableOutputs();
      gfx2->fillRoundRect(5, 5, 150, 70, 7, BLACK);
      gfx2->fillRoundRect(7, 7, 146, 66, 5, RED);
      gfx2->fillRoundRect(9, 9, 142, 62, 3, BLACK);
      gfx2->fillRoundRect(16, 11, 5, 10, 1, RED); 
      gfx2->fillCircle(18, 25, 2, RED); 
      gfx2->setTextColor(WHITE);
      gfx2->setTextSize(1);
      gfx2->setCursor(27, 23);
      gfx2->println("Homing error,");
      gfx2->setCursor(13, 41);
      gfx2->println("leveling canceled."); 
      gfx2->fillRoundRect(82, 51, 67, 18, 2,  0x879F);
      gfx2->setCursor(100, 64);
      gfx2->println("OK :(");
      while(digitalRead(buttonOK) == HIGH);
      screen21();      
      break;      
    }     
    
    // Cancel Check
    if (digitalRead(buttonBack) == LOW){
      gfx2->fillRoundRect(2, 20, 156, 56, 3, BLACK); 
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(BLACK);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(BLACK);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      screen21();
      break;
    }
  }
  stepper.disableOutputs();
  delay(50); 
          
  if (digitalRead(end_stop)){
    stepper.setCurrentPosition(0);
    gfx2->fillRoundRect(0, 0, 160, 20, 3, ORANGE); 
    gfx2->setCursor(40, 14);
    gfx2->print("Homing OK");
    gfx2->fillRect(2, 20, 156, 38, BLACK); 
    gfx2->setCursor(6, 34);
    gfx2->print("After fine adjustment");
    gfx2->setCursor(6, 52);
    gfx2->println("press next to lift it.");
    gfx2->fillRoundRect(82, 58, 72, 18, 2,  0x879F);
    gfx2->setCursor(102, 71);
    gfx2->println("Next");
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 214: Lift Build Plate
 * Lifts the build plate to maximum height.
 */
void screen214(){
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, ORANGE);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);
  gfx2->fillRoundRect(0, 0, 160, 20, 3, ORANGE);  
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(20, 14);
  gfx2->print("Lifting build plate");
  gfx2->setCursor(36, 43);
  gfx2->print("Please wait...");
  gfx2->fillRoundRect(6, 58, 72, 18, 2, ORANGE);
  gfx2->setCursor(16, 71);
  gfx2->println("Cancel");
  screen = 214;

  long lift_finished_print_steps = (max_height * steps_mm);
  stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
  stepper.enableOutputs();
  stepper.moveTo(lift_finished_print_steps); 
  while (stepper.distanceToGo()!= 0){
    stepper.run();
    if (digitalRead(buttonBack) == LOW){
      gfx2->fillRoundRect(2, 20, 156, 56, 3, BLACK); 
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(BLACK);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(BLACK);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      screen21();
      break;
    }
  }
  stepper.disableOutputs();
  delay(50);
  
  if(stepper.distanceToGo()== 0){
    gfx2->fillRoundRect(2, 20, 156, 56, 3, BLACK); 
    gfx2->setTextColor(WHITE);
    gfx2->setCursor(50, 43);
    gfx2->print("Done! :)");
    delay(600);
    gfx2->setTextColor(BLACK);
    gfx2->setCursor(50, 43);
    gfx2->print("Done! :)");
    delay(600);
    gfx2->setTextColor(WHITE);
    gfx2->setCursor(50, 43);
    gfx2->print("Done! :)");
    delay(600);
    gfx2->setTextColor(BLACK);
    gfx2->setCursor(50, 43);
    gfx2->print("Done! :)");
    delay(600);
    gfx2->setTextColor(WHITE);
    gfx2->setCursor(50, 43);
    gfx2->print("Done! :)");
    delay(600);
  }
  screen21();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 221: Manual Move Distance
 * Select manual movement distance (0.1, 1, 10 mm).
 */
void screen221(){
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(20, 36);
  gfx2->print("0.1");
  gfx2->setCursor(17, 52);
  gfx2->print("mm");
  gfx2->setCursor(76, 36);
  gfx2->print("1");
  gfx2->setCursor(67, 52);
  gfx2->print("mm");
  gfx2->setCursor(121, 36);
  gfx2->print("10");
  gfx2->setCursor(117, 52);
  gfx2->print("mm");
  gfx2->drawRoundRect(10, 20, 40, 40, 5, WHITE);
  screen = 221;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 222: Manual Move Highlight 1mm
 */
void screen222(){
  gfx2->drawRoundRect(10, 20, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(60, 20, 40, 40, 5, WHITE);
  gfx2->drawRoundRect(110, 20, 40, 40, 5, BLACK);
  screen = 222;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 223: Manual Move Highlight 10mm
 */
void screen223(){
  gfx2->drawRoundRect(10, 20, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(60, 20, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(110, 20, 40, 40, 5, WHITE);
  screen = 223;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 2211: Up/Down Indication 0.1mm
 * Shows arrows indicating manual control available.
 */
void screen2211(){
  gfx2->fillTriangle(29, 5, 23, 11, 35, 11, WHITE);
  gfx2->fillTriangle(29, 74, 23, 68, 35, 68, WHITE);
  screen = 2211;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 2221: Up/Down Indication 1mm
 */
void screen2221(){
  gfx2->fillTriangle(79, 5, 73, 11, 85, 11, WHITE);
  gfx2->fillTriangle(79, 74, 73, 68, 85, 68, WHITE);
  screen = 2221;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 2231: Up/Down Indication 10mm
 */
void screen2231(){
  gfx2->fillTriangle(129, 5, 123, 11, 135, 11, WHITE);
  gfx2->fillTriangle(129, 74, 123, 68, 135, 68, WHITE);
  screen = 2231;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 231: Clean Vat Instruction
 * Shows instructions for cleaning resin vat.
 */
void screen231(){
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, RED);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED); 
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(20, 16);
  gfx2->println("Please read the");
  gfx2->setCursor(6, 34);
  gfx2->println("instruction carefully");
  gfx2->setCursor(6, 52);
  gfx2->println("before doing this.");
  gfx2->fillRoundRect(6, 58, 72, 18, 2, ORANGE);
  gfx2->setCursor(24, 71);
  gfx2->println("Back");
  gfx2->fillRoundRect(82, 58, 72, 18, 2,  0x879F);
  gfx2->setCursor(102, 71);
  gfx2->println("Start");
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, BLACK); 
  gfx2->fillCircle(11, 18, 2, BLACK);
  delay(200);
  gfx2->fillRoundRect(9, 4, 5, 10, 1, RED); 
  gfx2->fillCircle(11, 18, 2, RED);
  screen = 231;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 2311: Set Exposure Time
 * UI for setting the duration of the vat cleaning exposure.
 */
void screen2311(){
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, ORANGE);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);
  gfx2->fillRoundRect(0, 0, 160, 20, 3, ORANGE);  
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(18, 14);
  gfx2->print("Set exposure time");
  gfx2->drawRoundRect(26, 27, 108, 24, 3, WHITE);
  gfx2->fillTriangle(125, 32, 122, 35, 128, 35, WHITE);
  gfx2->fillTriangle(125, 45, 122, 42, 128, 42, WHITE);     
  gfx2->fillRect(29, 30, 92, 18, BLACK);
  gfx2->setCursor(33, 43);
  gfx2->print(manual_exposure);  
  gfx2->fillRoundRect(6, 58, 72, 18, 2, ORANGE);
  gfx2->setCursor(24, 71);
  gfx2->println("Back");
  gfx2->fillRoundRect(82, 58, 72, 18, 2,  0x879F);
  gfx2->setCursor(102, 71);
  gfx2->println("Start");
  screen = 2311;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Increase Exposure Time
 * Increments manual exposure time (max 60s).
 */
void screen2311increase(){
  if(manual_exposure < 60)
  manual_exposure ++;
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);     
  gfx2->fillRect(29, 30, 92, 18, BLACK);
  gfx2->setCursor(33, 43);
  gfx2->print(manual_exposure);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Decrease Exposure Time
 * Decrements manual exposure time (min 10s).
 */
void screen2311decrease(){
  if(manual_exposure > 30)
  manual_exposure --;
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);     
  gfx2->fillRect(29, 30, 92, 18, BLACK);
  gfx2->setCursor(33, 43);
  gfx2->print(manual_exposure);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 23111: Execute Cleaning Exposure
 * Turns on UV LED for specified duration with progress bar.
 */
void screen23111(){
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, ORANGE);
  gfx2->fillRect(2, 20, 156, 34, BLACK);
  gfx2->fillRoundRect(2, 56, 156, 22, 3, BLACK);    
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(6, 14);
  gfx2->print("Full screen exposure"); 
  gfx2->fillRoundRect(6, 58, 72, 18, 2, ORANGE);
  gfx2->setCursor(16, 71);
  gfx2->println("Cancel");
  screen = 23111;

  long ExposureMillis = manual_exposure * 1000;
  byte buttonBackClicked = 0;    
  digitalWrite(FAN, HIGH);
  gfx1->fillScreen(WHITE);
  digitalWrite(LED, uvLedEnabled ? HIGH : LOW);
  startTime = millis();
  Duration = 0;
  while (Duration <= ExposureMillis){
    Duration = millis()-startTime;
    if (digitalRead(buttonBack) == LOW){
      buttonBackClicked = 1;
      gfx1->fillScreen(BLACK);      
      digitalWrite(LED, LOW);
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->fillRect(2, 20, 156, 34, BLACK);
      gfx2->fillRect(2, 20, 156*Duration/ExposureMillis, 34, PURPLE);
      delay(600);
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->fillRect(2, 20, 156, 34, BLACK);
      gfx2->fillRect(2, 20, 156*Duration/ExposureMillis, 34, PURPLE);
      delay(600);
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600); 
      digitalWrite(FAN, LOW);    
      break;      
    }    
    // Update Progress Bar
    gfx2->fillRect(2, 20, 156*Duration/ExposureMillis, 34, PURPLE);
  }
  if (uvLedEnabled) {  // LED aging: cleaning exposures count too
    totalUvLedSecs += Duration / 1000UL;
    saveUvLedTime();
  }
  if(buttonBackClicked == 0){
    gfx1->fillScreen(BLACK);
    digitalWrite(LED, LOW);
    gfx2->setTextColor(WHITE);
    gfx2->setCursor(50, 43);
    gfx2->print("Done! :)");
    delay(600);
    gfx2->fillRect(2, 20, 156, 34, PURPLE);
    delay(600);
    gfx2->setTextColor(WHITE);
    gfx2->setCursor(50, 43);
    gfx2->print("Done! :)");
    delay(600);
    gfx2->fillRect(2, 20, 156, 34, PURPLE);
    delay(600);
    gfx2->setTextColor(WHITE);
    gfx2->setCursor(50, 43);
    gfx2->print("Done! :)");
    delay(600);  
    digitalWrite(FAN, LOW);
  }
  screen2311();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Exposure calibration test (Advanced menu, screens 232/2321). Cures an
 * 8-bar strip straight from the masking LCD - no slicer, no SD. Bar N stays
 * lit for its own time on a ladder around the configured Regular exposure
 * (bar 1 shortest, bar 8 longest); the user picks the bar that cured crisply
 * and dials that time into Settings.
 */
// Proportional ladder (0.14.1): fixed multipliers of the Regular setting,
// bar 5 = 100% = your current value. A fast resin (cures in 3 s) and a slow
// one (25 s) both get a meaningful spread - fixed +-seconds steps did not
// (first real strip saturated: every bar past ~8 s looked identical).
// Whole seconds cannot hold a +-60% spread once Regular drops low: at 1 s the
// percentages all rounded to the same value and the strip burned eight
// identical bars that blanked at once (user finding, 0.15.0 testing). Exposure
// is settable only in whole seconds (one EEPROM byte, 1..30), so a sub-second
// ladder would produce bars nobody can then set. Below ~5 s the ladder
// therefore stops being proportional and becomes a 1 s sweep - every bar stays
// distinct and every bar maps to a value the pick can actually apply. From ~6 s
// up the percentages already separate on their own and nothing changes here.
int expTestBarSecs(int bar) {          // bar 1..8 -> seconds, always distinct
  static const uint8_t pct[8] = {40, 55, 70, 85, 100, 115, 135, 160};
  int t[8];
  for (int i = 0; i < 8; i++) {
    t[i] = ((int)Regular_Exposure * pct[i] + 50) / 100;
    if (t[i] < 1) t[i] = 1;                          // 1 s = the settable floor
    if (i && t[i] <= t[i - 1]) t[i] = t[i - 1] + 1;
  }
  return t[bar - 1];
}

void screenExpTestIntro(){
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 18);
  gfx2->print("Exposure test strip");
  gfx2->setTextColor(0x879F);
  gfx2->setCursor(8, 34);
  // gfx2 is 160 px wide and text starts at x=8, so a line has ~152 px. Arduino_GFX
  // wraps by default, and a wrapped line lands on top of the next one - every
  // string on these screens is measured to fit FreeSans8pt7b at its widest value.
  gfx2->print("Resin in vat, no plate");
  gfx2->setCursor(8, 48);
  gfx2->print(String("Cures 8 bars: ") + expTestBarSecs(1) + "-" + expTestBarSecs(8) + "s");
  uiButtons("Back", "Start", 0x879F);
  screen = 232;
}

void runExpTest(){
  // Running screen - modeled on the Clean Resin Vat exposure (23111)
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, ORANGE);
  gfx2->fillRect(2, 20, 156, 34, BLACK);
  gfx2->fillRoundRect(2, 56, 156, 22, 3, BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(6, 14);
  gfx2->print("Exposure test");
  gfx2->fillRoundRect(6, 58, 72, 18, 2, ORANGE);
  gfx2->setCursor(16, 71);
  gfx2->println("Cancel");

  // 8 vertical bars on the masking LCD; one continuous exposure, the
  // shortest bar is blanked first, so bar N's lit time = its ladder step.
  int W = gfx1->width(), H = gfx1->height();
  int slot = W / 8, gap = slot / 6, bw = slot - 2 * gap;
  int by = H / 6, bh = H - 2 * (H / 6);
  gfx1->fillScreen(BLACK);
  // Bar N carries N holes (dots) near its top, so a peeled strip stays
  // identifiable no matter how it is flipped - count dots, not sides.
  int r = bh / 60;
  if (r < 4) r = 4;
  for (int i = 0; i < 8; i++) {
    int x0 = i * slot + gap;
    gfx1->fillRect(x0, by, bw, bh, WHITE);
    for (int k = 0; k <= i; k++)
      gfx1->fillCircle(x0 + bw / 2, by + 3 * r + k * 3 * r, r, BLACK);
  }

  long maxMs = (long)expTestBarSecs(8) * 1000L;
  int blanked = 0;
  bool canceled = false;
  digitalWrite(FAN, HIGH);
  digitalWrite(LED, uvLedEnabled ? HIGH : LOW);
  startTime = millis();
  Duration = 0;
  while (Duration <= maxMs && !canceled){
    Duration = millis() - startTime;
    while (blanked < 8 && Duration >= (long)expTestBarSecs(blanked + 1) * 1000L){
      gfx1->fillRect(blanked * slot + gap, by, bw, bh, BLACK);
      blanked++;
    }
    if (digitalRead(buttonBack) == LOW) canceled = true;
    gfx2->fillRect(2, 20, 156 * Duration / maxMs, 34, PURPLE);
  }
  gfx1->fillScreen(BLACK);
  digitalWrite(LED, LOW);
  digitalWrite(FAN, LOW);
  if (uvLedEnabled) {                 // LED aging: test exposures count too
    totalUvLedSecs += Duration / 1000UL;
    saveUvLedTime();
  }

  // Result screen: what the bars mean, left to right
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 18);
  gfx2->print(canceled ? "Test canceled" : "Test strip done");
  if (!canceled){
    gfx2->setTextColor(0x879F);
    gfx2->setCursor(8, 34);
    gfx2->print(String("Dots 1..8 = ") + expTestBarSecs(1) + ".." + expTestBarSecs(8) + "s");
    gfx2->setCursor(8, 48);
    gfx2->print("Rinse, then pick bar");
    uiButtons("Skip", "Pick", 0x879F);
  } else {
    uiButtons("Back", "OK", 0x879F);
  }
  screen = canceled ? 23211 : 2321;
}

// Post-test result entry (screen 2322): the user counts the dots on the best
// bar and the printer sets Regular exposure itself - no manual Settings trip.
// Positions 9/10 shift the whole ladder down/up for a re-run when no bar was
// right (every bar fat -> shorter; every bar soft/rubbery -> longer).
// Kept behind functions: globals in later .ino tabs are not visible from
// TinyMaker.ino's button handlers (only functions get auto-prototypes).
int expTestPick = 5;

void expTestPickStart(){ expTestPick = 5; screenExpTestPick(); }
void expTestPickNext(){ expTestPick = expTestPick % 10 + 1; screenExpTestPick(); }

void screenExpTestPick(){
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 18);
  gfx2->print("Best bar (count dots)");
  gfx2->setTextColor(0x879F);
  gfx2->setCursor(8, 38);
  if (expTestPick <= 8) {
    gfx2->print(String(expTestPick) + " dots -> " + expTestBarSecs(expTestPick) + "s");
    if (expTestBarSecs(expTestPick) == (int)Regular_Exposure) gfx2->print(" (now)");
  } else if (expTestPick == 9) {
    gfx2->print(String("All fat -> ") + expTestBarSecs(1) + "s");
  } else {
    gfx2->print(String("All soft -> ") + expTestBarSecs(8) + "s");
  }
  gfx2->setCursor(8, 52);
  gfx2->print(expTestPick <= 8 ? "UP = next" : "UP = next (retest)");
  gfx2->setTextColor(WHITE);
  uiButtons("Skip", "Set", 0x879F);
  screen = 2322;
}

void expTestApplyPick(){
  int t = expTestBarSecs(expTestPick <= 8 ? expTestPick : (expTestPick == 9 ? 1 : 8));
  if (t < 1) t = 1;
  if (t > 30) t = 30;
  long oldR = Regular_Exposure;
  Regular_Exposure = t;
  savePrintSettings();
  rememberPrevRegularExposure(oldR);
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 18);
  gfx2->print(expTestPick <= 8 ? "Exposure set" : "Ladder shifted");
  gfx2->setTextColor(0x879F);
  gfx2->setCursor(8, 38);
  gfx2->print(String("Regular exp = ") + t + "s");
  gfx2->setCursor(8, 52);
  gfx2->print(expTestPick <= 8 ? "Base ~2.5x is typical" : "Run the test again");
  delay(2000);
  screenAdvancedOptions();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 31 UP: Settings Menu Navigation (Previous)
 * Navigates up through the settings list.
 */
void screen31UP(){  
  if (setting_item > 1) {
    setting_item --;
    gfx2->fillScreen(BLACK);
    gfx2->setFont(&FreeSans8pt7b);
    gfx2->setTextColor(WHITE);
    gfx2->setTextSize(1);
    // Draw Menu Items based on current selection    
    switch (setting_item) {
      case 1:
      gfx2->setCursor(5, 15);
      gfx2->println("Layer Height"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Layer_Height); 
      gfx2->print(" "); 
      gfx2->print("mm"); 
      gfx2->setCursor(5, 56);
      gfx2->println("Base Exposure");
      gfx2->setCursor(5, 74);
      gfx2->print(Base_Exposure);
      gfx2->print(" "); 
      gfx2->print("s");
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE);   
        break;
      case 2:
      gfx2->setCursor(5, 15);
      gfx2->println("Base Exposure"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Base_Exposure);
      gfx2->print(" "); 
      gfx2->print("s");  
      gfx2->setCursor(5, 56);
      gfx2->println("Regular Exposure");
      gfx2->setCursor(5, 74);
      gfx2->print(Regular_Exposure);
      gfx2->print(" "); 
      gfx2->print("s");      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE); 
        break;
      case 3:
      gfx2->setCursor(5, 15);
      gfx2->println("Regular Exposure"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Regular_Exposure); 
      gfx2->print(" "); 
      gfx2->print("s");  
      gfx2->setCursor(5, 56);
      gfx2->println("Base Layer");
      gfx2->setCursor(5, 74);
      gfx2->print(Base_Layer);      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE);
        break;
      case 4:
      gfx2->setCursor(5, 15);
      gfx2->println("Base Layer"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Base_Layer); 
      gfx2->setCursor(5, 56);
      gfx2->println("Transition Layer");
      gfx2->setCursor(5, 74);
      gfx2->print(Transition_Layer);      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE);
        break;
      case 5:
      gfx2->setCursor(5, 15);
      gfx2->println("Transition Layer"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Transition_Layer);  
      gfx2->setCursor(5, 56);
      gfx2->println("Slow Lift Distance");
      gfx2->setCursor(5, 74);
      gfx2->print(Slow_Lift_Distance);
      gfx2->print(" "); 
      gfx2->print("mm");       
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE);
        break;
      case 6:
      gfx2->setCursor(5, 15);
      gfx2->println("Slow Lift Distance"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Slow_Lift_Distance);
      gfx2->print(" "); 
      gfx2->print("mm");  
      gfx2->setCursor(5, 56);
      gfx2->println("Fast Lift Distance");
      gfx2->setCursor(5, 74);
      gfx2->print(Fast_Lift_Distance);
      gfx2->print(" "); 
      gfx2->print("mm");      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE); 
        break;
      case 7:
      gfx2->setCursor(5, 15);
      gfx2->println("Fast Lift Distance"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Fast_Lift_Distance);
      gfx2->print(" "); 
      gfx2->print("mm");  
      gfx2->setCursor(5, 56);
      gfx2->println("Slow Lift Feedrate");
      gfx2->setCursor(5, 74);
      gfx2->print(Slow_Lift_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE); 
        break;
      case 8:
      gfx2->setCursor(5, 15);
      gfx2->println("Slow Lift Feedrate"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Slow_Lift_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");  
      gfx2->setCursor(5, 56);
      gfx2->println("Fast Lift Feedrate");
      gfx2->setCursor(5, 74);
      gfx2->print(Fast_Lift_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE); 
        break;
      case 9:
      gfx2->setCursor(5, 15);
      gfx2->println("Fast Lift Feedrate"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Fast_Lift_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");  
      gfx2->setCursor(5, 56);
      gfx2->println("Drop Back Feedrate");
      gfx2->setCursor(5, 74);
      gfx2->print(Drop_Back_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE); 
        break; 
      case 10:
      gfx2->setCursor(5, 15);
      gfx2->println("Drop Back Feedrate"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Drop_Back_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");  
      gfx2->setCursor(5, 56);
      gfx2->println("VAT size");
      gfx2->setCursor(5, 74);
      gfx2->print(Vat_Capacity_Ml);
      gfx2->print(" ");
      gfx2->print("ml");
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE); 
        break;
      case 11:
      gfx2->setCursor(5, 15);
      gfx2->println("VAT size");
      gfx2->setCursor(5, 33);
      gfx2->print(Vat_Capacity_Ml);
      gfx2->print(" ");
      gfx2->print("ml");
      gfx2->setCursor(5, 56);
      gfx2->println("Back to Default");
      gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE);
        break;
    }
  }
  else{
    setting_item = 1;
    gfx2->fillScreen(BLACK);
    gfx2->setFont(&FreeSans8pt7b);
    gfx2->setTextColor(WHITE);
    gfx2->setTextSize(1);
    gfx2->setCursor(5, 15);
    gfx2->println("Layer Height"); 
    gfx2->setCursor(5, 33);
    gfx2->print(Layer_Height); 
    gfx2->print(" "); 
    gfx2->print("mm"); 
    gfx2->setCursor(5, 56);
    gfx2->println("Base Exposure");
    gfx2->setCursor(5, 74);
    gfx2->print(Base_Exposure);
    gfx2->print(" "); 
    gfx2->print("s");   
    gfx2->drawRoundRect(0, 41, 160, 39, 3, BLACK);
    gfx2->drawRoundRect(0, 0, 160, 39, 3, WHITE);
    delay(300);   
  }
  screen = 31;
  setting_item_updown = 1;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 31 DOWN: Settings Menu Navigation (Next)
 * Navigates down through the settings list.
 */
void screen31DOWN(){
  if (setting_item < 12) {
    setting_item ++;
    gfx2->fillScreen(BLACK);
    gfx2->setFont(&FreeSans8pt7b);
    gfx2->setTextColor(WHITE);
    gfx2->setTextSize(1);
    switch (setting_item) {
      case 2:
      gfx2->setCursor(5, 15);
      gfx2->println("Layer Height"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Layer_Height); 
      gfx2->print(" "); 
      gfx2->print("mm"); 
      gfx2->setCursor(5, 56);
      gfx2->println("Base Exposure");
      gfx2->setCursor(5, 74);
      gfx2->print(Base_Exposure);
      gfx2->print(" "); 
      gfx2->print("s");
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK);   
        break;
      case 3:
      gfx2->setCursor(5, 15);
      gfx2->println("Base Exposure"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Base_Exposure);
      gfx2->print(" "); 
      gfx2->print("s");  
      gfx2->setCursor(5, 56);
      gfx2->println("Regular Exposure");
      gfx2->setCursor(5, 74);
      gfx2->print(Regular_Exposure);
      gfx2->print(" "); 
      gfx2->print("s");       
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK); 
        break;
      case 4:
      gfx2->setCursor(5, 15);
      gfx2->println("Regular Exposure"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Regular_Exposure); 
      gfx2->print(" "); 
      gfx2->print("s"); 
      gfx2->setCursor(5, 56);
      gfx2->println("Base Layer");
      gfx2->setCursor(5, 74);
      gfx2->print(Base_Layer);      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK);
        break;
      case 5:
      gfx2->setCursor(5, 15);
      gfx2->println("Base Layer"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Base_Layer); 
      gfx2->setCursor(5, 56);
      gfx2->println("Transition Layer");
      gfx2->setCursor(5, 74);
      gfx2->print(Transition_Layer);      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK);
        break;
      case 6:
      gfx2->setCursor(5, 15);
      gfx2->println("Transition Layer"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Transition_Layer);  
      gfx2->setCursor(5, 56);
      gfx2->println("Slow Lift Distance");
      gfx2->setCursor(5, 74);
      gfx2->print(Slow_Lift_Distance);
      gfx2->print(" "); 
      gfx2->print("mm");       
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK);
        break;
      case 7:
      gfx2->setCursor(5, 15);
      gfx2->println("Slow Lift Distance"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Slow_Lift_Distance);
      gfx2->print(" "); 
      gfx2->print("mm");  
      gfx2->setCursor(5, 56);
      gfx2->println("Fast Lift Distance");
      gfx2->setCursor(5, 74);
      gfx2->print(Fast_Lift_Distance);
      gfx2->print(" "); 
      gfx2->print("mm");      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK); 
        break;
      case 8:
      gfx2->setCursor(5, 15);
      gfx2->println("Fast Lift Distance"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Fast_Lift_Distance);
      gfx2->print(" "); 
      gfx2->print("mm");  
      gfx2->setCursor(5, 56);
      gfx2->println("Slow Lift Feedrate");
      gfx2->setCursor(5, 74);
      gfx2->print(Slow_Lift_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK); 
        break;
      case 9:
      gfx2->setCursor(5, 15);
      gfx2->println("Slow Lift Feedrate"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Slow_Lift_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");  
      gfx2->setCursor(5, 56);
      gfx2->println("Fast Lift Feedrate");
      gfx2->setCursor(5, 74);
      gfx2->print(Fast_Lift_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK); 
        break;
      case 10:
      gfx2->setCursor(5, 15);
      gfx2->println("Fast Lift Feedrate"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Fast_Lift_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");  
      gfx2->setCursor(5, 56);
      gfx2->println("Drop Back Feedrate");
      gfx2->setCursor(5, 74);
      gfx2->print(Drop_Back_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");      
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK); 
        break; 
      case 11:
      gfx2->setCursor(5, 15);
      gfx2->println("Drop Back Feedrate"); 
      gfx2->setCursor(5, 33);
      gfx2->print(Drop_Back_Feedrate);
      gfx2->print(" "); 
      gfx2->print("mm/min");  
      gfx2->setCursor(5, 56);
      gfx2->println("VAT size");
      gfx2->setCursor(5, 74);
      gfx2->print(Vat_Capacity_Ml);
      gfx2->print(" ");
      gfx2->print("ml");
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK); 
        break;
      case 12:
      gfx2->setCursor(5, 15);
      gfx2->println("VAT size");
      gfx2->setCursor(5, 33);
      gfx2->print(Vat_Capacity_Ml);
      gfx2->print(" ");
      gfx2->print("ml");
      gfx2->setCursor(5, 56);
      gfx2->println("Back to Default");
      gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
      gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK); 
        break;
    }
  }
  else{
    gfx2->fillScreen(BLACK);
    gfx2->setFont(&FreeSans8pt7b);
    gfx2->setTextColor(WHITE);
    gfx2->setTextSize(1);
    gfx2->setCursor(5, 15);
    gfx2->println("Drop Back Feedrate"); 
    gfx2->setCursor(5, 33);
    gfx2->print(Drop_Back_Feedrate);
    gfx2->print(" "); 
    gfx2->print("mm/min");  
    gfx2->setCursor(5, 56);
    gfx2->println("Back to Default");     
    gfx2->drawRoundRect(0, 41, 160, 39, 3, WHITE);
    gfx2->drawRoundRect(0, 0, 160, 39, 3, BLACK); 
  }
  screen = 31;
  setting_item_updown = 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Screen 311: Edit/Select Logic
 * Handles triangle selection indicators and "Back to Default" reset.
 */
void screen311(){
  if (setting_item_updown == 1) {
    // Top Option Selected
    gfx2->fillTriangle(151, 20, 148, 23, 154, 23, WHITE); 
    gfx2->fillTriangle(151, 33, 148, 30, 154, 30, WHITE);
    screen = 311;  
  }
  if (setting_item_updown == 0 || setting_item == 12) {
    if(setting_item != 12){
      // Bottom Option Selected
      gfx2->fillTriangle(151, 61, 148, 64, 154, 64, WHITE); 
      gfx2->fillTriangle(151, 74, 148, 71, 154, 71, WHITE); 
      screen = 311;
    }
    else{
      // "Back to Default" Selected -> Reset EEPROM to factory defaults
      // (shared with setup(); defined in TinyMaker.ino)
      resetSettingsToDefault();

      setting_item = 11;
      screen31DOWN(); // Refresh Screen
    }
  }
  delay(300);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Increase Setting Value
 * Increments the selected setting parameter.
 */
void screen3111increase(){
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  if (setting_item_updown == 1) {
    switch (setting_item){
      case 1:
      if(Layer_Height < 0.09){
        Layer_Height = 0.1;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Layer_Height);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break;
      case 2:
      if(Base_Exposure < 60){
        Base_Exposure ++;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Base_Exposure);
        gfx2->print(" "); 
        gfx2->print("s");
      }
        break;
      case 3:
      if(Regular_Exposure < 30){
        Regular_Exposure ++;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Regular_Exposure);
        gfx2->print(" "); 
        gfx2->print("s");
      }
        break; 
      case 4:
      if(Base_Layer < 8){
        Base_Layer ++;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Base_Layer);
      }
        break; 
      case 5:
      if(Transition_Layer < 10){
        Transition_Layer ++;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Transition_Layer);
      }
        break;  
      case 6:
      if(Slow_Lift_Distance < 3){
        Slow_Lift_Distance ++;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Slow_Lift_Distance);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break;  
      case 7:
      if(Fast_Lift_Distance < 3){
        Fast_Lift_Distance ++;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Fast_Lift_Distance);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break; 
      case 8:
      if(Slow_Lift_Feedrate < 50){
        Slow_Lift_Feedrate += 10;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Slow_Lift_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break; 
      case 9:
      if(Fast_Lift_Feedrate < 50){
        Fast_Lift_Feedrate += 10;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Fast_Lift_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break;
      case 10:
      if(Drop_Back_Feedrate < 50){
        Drop_Back_Feedrate += 10;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Drop_Back_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break;
      case 11:
      if(Vat_Capacity_Ml < 40){
        Vat_Capacity_Ml ++;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Vat_Capacity_Ml);
        gfx2->print(" ");
        gfx2->print("ml");
      }
        break;
    }
  }
 
  if (setting_item_updown == 0) {
    switch (setting_item){
      case 2:
      if(Base_Exposure < 60){
        Base_Exposure ++;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Base_Exposure);
        gfx2->print(" "); 
        gfx2->print("s");
      }
        break; 
      case 3:
      if(Regular_Exposure < 30){
        Regular_Exposure ++;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Regular_Exposure);
        gfx2->print(" "); 
        gfx2->print("s");
      }
        break;
      case 4:
      if(Base_Layer < 8){
        Base_Layer ++;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Base_Layer);
      }
        break;  
      case 5:
      if(Transition_Layer < 10){
        Transition_Layer ++;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Transition_Layer);
      }
        break;  
      case 6:
      if(Slow_Lift_Distance < 3){
        Slow_Lift_Distance ++;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Slow_Lift_Distance);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break;  
      case 7:
      if(Fast_Lift_Distance < 3){
        Fast_Lift_Distance ++;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Fast_Lift_Distance);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break;
      case 8:
      if(Slow_Lift_Feedrate < 50){
        Slow_Lift_Feedrate += 10;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Slow_Lift_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break; 
      case 9:
      if(Fast_Lift_Feedrate < 50){
        Fast_Lift_Feedrate += 10;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Fast_Lift_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break;
      case 10:
      if(Drop_Back_Feedrate < 50){
        Drop_Back_Feedrate += 10;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Drop_Back_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break;
      case 11:
      if(Vat_Capacity_Ml < 40){
        Vat_Capacity_Ml ++;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Vat_Capacity_Ml);
        gfx2->print(" ");
        gfx2->print("ml");
      }
        break;   
    }    
  }
  screen = 3111;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Decrease Setting Value
 * Decrements the selected setting parameter.
 */
void screen3111decrease(){
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  
  if (setting_item_updown == 1) {
    switch (setting_item){
      case 1:
      if(Layer_Height > 0.06){
        Layer_Height = 0.05;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Layer_Height);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break;
      case 2:
      if(Base_Exposure > 10){
        Base_Exposure --;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Base_Exposure);
        gfx2->print(" "); 
        gfx2->print("s");
      }
        break;
      case 3:
      if(Regular_Exposure > 1){
        Regular_Exposure --;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Regular_Exposure);
        gfx2->print(" "); 
        gfx2->print("s");
      }
        break; 
      case 4:
      if(Base_Layer > 1){
        Base_Layer --;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Base_Layer);
      }
        break;  
      case 5:
      if(Transition_Layer > 0){
        Transition_Layer --;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Transition_Layer);
      }
        break;  
      case 6:
      if(Slow_Lift_Distance > 1){
        Slow_Lift_Distance --;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Slow_Lift_Distance);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break;  
      case 7:
      if(Fast_Lift_Distance > 1){
        Fast_Lift_Distance --;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Fast_Lift_Distance);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break; 
      case 8:
      if(Slow_Lift_Feedrate > 20){
        Slow_Lift_Feedrate -= 10;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Slow_Lift_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break; 
      case 9:
      if(Fast_Lift_Feedrate > 20){
        Fast_Lift_Feedrate -= 10;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Fast_Lift_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break;  
      case 10:
      if(Drop_Back_Feedrate > 20){
        Drop_Back_Feedrate -= 10;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Drop_Back_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break;
      case 11:
      if(Vat_Capacity_Ml > 10){
        Vat_Capacity_Ml --;
        gfx2->fillRect(3, 20, 80, 17, BLACK);
        gfx2->setCursor(5, 33);
        gfx2->print(Vat_Capacity_Ml);
        gfx2->print(" ");
        gfx2->print("ml");
      }
        break;   
    }
  }
 
  if (setting_item_updown == 0) {
    switch (setting_item){
      case 2:
      if(Base_Exposure > 10){
        Base_Exposure --;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Base_Exposure);
        gfx2->print(" "); 
        gfx2->print("s");
      }
        break;
      case 3:
      if(Regular_Exposure > 1){
        Regular_Exposure --;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Regular_Exposure);
        gfx2->print(" "); 
        gfx2->print("s");
      }
        break;
      case 4:
      if(Base_Layer > 1){
        Base_Layer --;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Base_Layer);
      }
        break;  
      case 5:
      if(Transition_Layer > 0){
        Transition_Layer --;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Transition_Layer);
      }
        break;  
      case 6:
      if(Slow_Lift_Distance > 1){
        Slow_Lift_Distance --;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Slow_Lift_Distance);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break;  
      case 7:
      if(Fast_Lift_Distance > 1){
        Fast_Lift_Distance --;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Fast_Lift_Distance);
        gfx2->print(" "); 
        gfx2->print("mm");
      }
        break;    
      case 8:
      if(Slow_Lift_Feedrate > 20){
        Slow_Lift_Feedrate -= 10;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Slow_Lift_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break;
      case 9:
      if(Fast_Lift_Feedrate > 20){
        Fast_Lift_Feedrate -= 10;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Fast_Lift_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break; 
      case 10:
      if(Drop_Back_Feedrate > 20){
        Drop_Back_Feedrate -= 10;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Drop_Back_Feedrate);
        gfx2->print(" "); 
        gfx2->print("mm/min");
      }
        break;
      case 11:
      if(Vat_Capacity_Ml > 10){
        Vat_Capacity_Ml --;
        gfx2->fillRect(3, 61, 80, 17, BLACK);
        gfx2->setCursor(5, 74);
        gfx2->print(Vat_Capacity_Ml);
        gfx2->print(" ");
        gfx2->print("ml");
      }
        break;
    }    
  }
  screen = 3111;
}
