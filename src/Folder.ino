/**
 * @brief Decide whether a directory entry appears in the Print list.
 * Two kinds of entries qualify: a printable model folder (contains 1.png)
 * and an importable .sl1/.zip archive in the SD root (OK converts it).
 * Sets the global selIsArchive for the accepted entry.
 */
bool listEntryValid(File &entry) {
  char name[101];
  entry.getName(name, sizeof(name));
  if (entry.isDirectory()) {
    FileName = name;
    FileName += "/1.png";
    File probe = SD.open(FileName);
    if (!probe) return false;
    probe.close();
    selIsArchive = false;
    return true;
  }
  String n = String(name);
  n.toLowerCase();
  if (n.endsWith(".sl1") || n.endsWith(".zip")) {
    selIsArchive = true;
    return true;
  }
  return false;
}

/**
 * @brief Draw the currently selected list entry into the Select File box.
 * Archives are drawn in blue (import), model folders in white (print).
 * Names are trimmed by pixel width to fit the box; for archives the
 * ".sl1"/".zip" extension always stays visible (only the base is trimmed).
 */
void drawListSelection() {
  String name = String(foldersel_long);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextSize(1);
  if (selIsArchive) {
    int dot = name.lastIndexOf('.');
    String base = name.substring(0, dot);
    String ext = name.substring(dot);          // ".sl1" / ".zip"
    foldersel = base + ext;
    while (base.length() > 1) {
      int16_t bx, by; uint16_t bw, bh;
      gfx2->getTextBounds(foldersel.c_str(), 0, 0, &bx, &by, &bw, &bh);
      if ((int)bw <= 128) break;               // fits the 137px box
      base.remove(base.length() - 1);
      foldersel = base + ext;
    }
  } else {
    foldersel = uiFitText(name, 128);
  }
  gfx2->fillRoundRect(7, 28, 137, 22, 2, BLACK);
  gfx2->setTextColor(selIsArchive ? 0x879F : WHITE);
  gfx2->setCursor(12, 43);
  gfx2->print(foldersel);
  gfx2->setTextColor(WHITE);
}

/**
 * @brief Folder Down Navigation
 * Browses to the next list entry (model folder or importable archive).
 *
 * @param dir The directory to read from
 */
void folderDown(File dir) {
  counter++;
  for (int i = 0; i < counter; i++) {
    while (true) {
      File entry = dir.openNextFile();
      if (! entry) {
        counter--;
        break;
      }
      if (listEntryValid(entry)) {
        entry.getName(foldersel_long, 101);
        entry.close();
        break;
      }
      entry.close();
    }
  }
  drawListSelection();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Folder Up Navigation
 * Browses to the previous list entry.
 *
 * @param dir The directory to read from
 */
void folderUp(File dir) {
  if (counter > 1) {
    counter --;
    for (int i = 0; i < counter; i++) {
      while (true) {
        File entry = dir.openNextFile();
        if (! entry) break;   // safety net (should not happen: entry exists)
        if (listEntryValid(entry)) {
          entry.getName(foldersel_long, 101);
          entry.close();
          break;
        }
        entry.close();
      }
    }
    drawListSelection();
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Delete a model folder from SD: removes all files inside, then the
 * folder itself. Used by the long-press-OK delete feature in the Print menu.
 */
bool deleteModelFolder(const char *path, bool showProgress = true) {
  // Pass 1: count entries (deleting hundreds of layer PNGs takes tens of
  // seconds on FAT, so we show a progress bar like the WiFi connect one)
  File dir = SD.open(path);
  if (!dir) return false;
  int total = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    total++;
    entry.close();
  }
  dir.close();
  if (total == 0) return SD.rmdir(path);

  // Pass 2: delete with progress
  dir = SD.open(path);
  if (!dir) return false;
  int done = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    char name[101];
    entry.getName(name, sizeof(name));
    bool isDir = entry.isDirectory();
    entry.close();
    String full = String(path) + "/" + String(name);
    if (isDir) SD.rmdir(full.c_str());
    else SD.remove(full.c_str());
    done++;
    if (showProgress && (done % 10 == 0 || done == total)) {
      int w = (int)(136L * done / total);
      if (w > 136) w = 136;
      if (w > 0) gfx2->fillRect(12, 50, w, 12, ORANGE);
    }
  }
  dir.close();
  return SD.rmdir(path);
}

/**
 * @brief Screen 113: delete confirmation for the selected model.
 * OK = delete (handled in loop), Back = return to the model list.
 */
void screenDeleteConfirm(){
  uiFrame(RED);   // red frame: destructive action
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 20);
  gfx2->print(selIsArchive ? "Delete file?" : "Delete model?");
  gfx2->setTextColor(ORANGE);
  gfx2->setCursor(8, 44);
  gfx2->print(foldersel);
  gfx2->setTextColor(WHITE);
  // Button colors match the physical buttons (blue = OK, orange = Back);
  // the red frame alone signals the destructive action.
  uiButtons("Back", "Delete", 0x879F);
  screen = 113;
}

/**
 * @brief Deletes the currently selected model folder and returns to Main Menu.
 */
void deleteSelectedModel(){
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 18);
  gfx2->print("Deleting:");
  gfx2->setCursor(5, 38);
  gfx2->print(foldersel);
  gfx2->drawRoundRect(10, 48, 140, 16, 3, WHITE);
  String path = "/" + String(foldersel_long);
  // Archives are single files; models are whole layer folders
  bool ok = selIsArchive ? SD.remove(path.c_str())
                         : deleteModelFolder(path.c_str());
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setCursor(8, 44);
  gfx2->print(ok ? "Deleted" : "Delete FAILED");
  delay(1200);
  screen1();
}
