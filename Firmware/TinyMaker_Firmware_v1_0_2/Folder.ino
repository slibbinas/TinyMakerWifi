/**
 * @brief Folder Down Navigation
 * Browses into the Next folder or displays file name.
 *
 * @param dir The directory to read from
 */
void folderDown(File dir) {
  counter++;  
  for (int i = 0; i < counter; i++) {
    while (true) {
      File entry =  dir.openNextFile();     
      if (! entry) {
        counter--;
        break;
      }
      if (entry.isDirectory()) {
        char foldertest[101];
        entry.getName(foldertest, 101);
        FileName = foldertest;
        FileName += "/1.png";
        File entry2 = SD.open(FileName);
        if (entry2){
          entry.getName(foldersel_long, 101);
          break;  
        }  
      }
      entry.close();
    }
  } 
    foldersel = String(foldersel_long);
    foldersel = foldersel.substring(0, 10);
    gfx2->fillRoundRect(7, 28, 137, 22, 2, BLACK);
    gfx2->setFont(&FreeSans8pt7b);
    gfx2->setTextColor(WHITE);
    gfx2->setTextSize(1);
    gfx2->setCursor(12, 43);
    gfx2->print(foldersel);  
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Folder Up Navigation
 * Browses to the Previous folder.
 *
 * @param dir The directory to read from
 */
void folderUp(File dir) {
  if (counter > 1) {
    counter --;
    for (int i = 0; i < counter; i++) {
      while (true) {
        File entry =  dir.openNextFile();
        /*if (! entry) {
          break;
        }*/
        if (entry.isDirectory()) {
          entry.getName(foldersel_long, 101);
          FileName = foldersel_long;
          FileName += "/1.png";
          File entry2 = SD.open(FileName);
          if (entry2)
            break;
        }
        entry.close();
      }
    }  
    foldersel = String(foldersel_long);
    foldersel = foldersel.substring(0, 10);
    gfx2->fillRoundRect(7, 28, 137, 22, 2, BLACK);
    gfx2->setFont(&FreeSans8pt7b);
    gfx2->setTextColor(WHITE);
    gfx2->setTextSize(1);
    gfx2->setCursor(12, 43);
    gfx2->print(foldersel);
  }
}
