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
  int w = (int)(136L * step / total);
  if (w > 136) w = 136;
  if (w > 0) gfx2->fillRect(12, 50, w, 12, ORANGE);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Screen 0: Welcome Screen
 * Displays "Hello, World!" message on startup.
 */
void screen0(){
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
  for (int i = 0; i < 40; i++) {
    gfx2->setCursor(40, i);
    gfx2->setTextColor(ORANGE);
    gfx2->println("Tiny Maker"); 
    delay(30);
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

  // Print icon (box 1: x0-40)
  gfx2->fillTriangle(9, 23, 19, 18, 29, 23, ORANGE);
  gfx2->fillTriangle(9, 23, 19, 28, 29, 23, ORANGE);
  gfx2->drawLine(9, 27, 19, 32, ORANGE);
  gfx2->drawLine(9, 31, 19, 36, ORANGE);
  gfx2->drawLine(9, 35, 19, 40, ORANGE);
  gfx2->drawLine(19, 32, 29, 27, ORANGE);
  gfx2->drawLine(19, 36, 29, 31, ORANGE);
  gfx2->drawLine(19, 40, 29, 35, ORANGE);

  // Maintenance icon (box 2: x40-80)
  gfx2->fillCircle(52, 37, 3, ORANGE); 
  gfx2->fillCircle(52, 37, 1, BLACK); 
  gfx2->fillCircle(65, 24, 6, ORANGE);
  gfx2->fillCircle(67, 22, 3, BLACK); 
  gfx2->fillCircle(68, 21, 3, BLACK);
  gfx2->fillTriangle(51, 34, 59, 26, 55, 38, ORANGE);
  gfx2->fillTriangle(55, 38, 63, 30, 59, 26, ORANGE);

  // Settings icon (box 3: x80-120)
  gfx2->fillRoundRect(91, 21, 20, 2, 1, ORANGE);
  gfx2->fillRoundRect(91, 28, 20, 2, 1, ORANGE);
  gfx2->fillRoundRect(91, 35, 20, 2, 1, ORANGE);
  gfx2->fillCircle(104, 21, 2, ORANGE);
  gfx2->fillCircle(99, 28, 2, ORANGE);
  gfx2->fillCircle(104, 35, 2, ORANGE);

  // System icon: gear (box 4: x120-160), drawn from a 1-bit 27x27 bitmap
  // (generated pixel map - 8 identical teeth, rounded tips, big hole;
  // shape approved by the maintainer). See gearBitmap above screen1().
  gfx2->drawBitmap(127, 19, gearBitmap, 27, 27, ORANGE);

  gfx2->drawRoundRect(0, 13, 40, 40, 5, 0x879F);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(63, 71);
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
  gfx2->drawRoundRect(0, 13, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(80, 13, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(120, 13, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(40, 13, 40, 40, 5, 0x879F);
  gfx2->fillRect(0, 53, 160, 27, BLACK);
  gfx2->setCursor(31, 71);
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
  gfx2->drawRoundRect(0, 13, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(40, 13, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(120, 13, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(80, 13, 40, 40, 5, 0x879F);
  gfx2->fillRect(0, 53, 160, 27, BLACK);
  gfx2->setCursor(51, 71);
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
  gfx2->drawRoundRect(0, 13, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(40, 13, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(80, 13, 40, 40, 5, BLACK);
  gfx2->drawRoundRect(120, 13, 40, 40, 5, 0x879F);
  gfx2->fillRect(0, 53, 160, 27, BLACK);
  gfx2->setCursor(53, 71);
  gfx2->print("System");
  #if ENABLE_NETWORK
  drawWifiBadge();
  #endif
  screen = 4;
}

/**
 * @brief Screens 41-43: System submenu (WiFi Info / About / Update)
 */
void screen41(){
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setCursor(7, 17);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->println("WiFi Info");
  gfx2->setCursor(7, 44);
  gfx2->print("Update");
  gfx2->setCursor(7, 71);
  gfx2->print("About");
  gfx2->drawRoundRect(1, 1, 158, 24, 3, WHITE);
  screen = 41;
}

void screen42(){
  gfx2->drawRoundRect(1, 1, 158, 24, 3, BLACK);
  gfx2->drawRoundRect(1, 28, 158, 24, 3, WHITE);
  gfx2->drawRoundRect(1, 55, 158, 24, 3, BLACK);
  screen = 42;
}

void screen43(){
  gfx2->drawRoundRect(1, 1, 158, 24, 3, BLACK);
  gfx2->drawRoundRect(1, 28, 158, 24, 3, BLACK);
  gfx2->drawRoundRect(1, 55, 158, 24, 3, WHITE);
  screen = 43;
}

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
#endif

/**
 * @brief Screen 421: Firmware update - installed vs latest, self-update.
 * Shows the installed version, checks GitHub Pages for the latest, and
 * offers "Install" (self-update, no PC). UP ("Local") opens screen 422 to
 * install a specific/older version from a file via the browser.
 */
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
  // Info block (11 px pitch), wider gap before the repo block; key labels
  // in blue for quick scanning
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
  gfx2->print("Printed: ");
  gfx2->setTextColor(WHITE);
  gfx2->print(totalPrintSecs / 3600UL);
  gfx2->print("h ");
  gfx2->print((totalPrintSecs % 3600UL) / 60UL);
  gfx2->print("m");
  gfx2->setCursor(5, 60);
  gfx2->print("github.com/slibbinas/");
  gfx2->setCursor(5, 71);
  gfx2->print("TinyMakerWifi");
  gfx2->setFont(&FreeSans8pt7b); // restore UI font
  screen = 431;
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
  gfx2->setCursor(20, 16);
  gfx2->println("Factory calibrated.");
  gfx2->setCursor(6, 34);
  gfx2->println("Re-level only when");
  gfx2->setCursor(6, 52);
  gfx2->println("needed.");
  
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
  if (setting_item < 11) {
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
  if (setting_item_updown == 0 || setting_item == 11) {
    if(setting_item != 11){
      // Bottom Option Selected
      gfx2->fillTriangle(151, 61, 148, 64, 154, 64, WHITE); 
      gfx2->fillTriangle(151, 74, 148, 71, 154, 71, WHITE); 
      screen = 311;
    }
    else{
      // "Back to Default" Selected -> Reset EEPROM to factory defaults
      // (shared with setup(); defined in TinyMaker.ino)
      resetSettingsToDefault();

      setting_item = 10;
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
    }    
  }
  screen = 3111;
}
