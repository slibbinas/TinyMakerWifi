// Open file for PNG library
// --- Resin volume estimation (white pixels = cured resin) ---
// One masking-LCD pixel area: 40.8 x 30.6 mm / (320 x 240) = 0.01626 mm^2
// (from the PrusaSlicer TinyMaker profile). Volume per layer =
// whitePixels * PX_AREA_MM2 * layerHeight (mm) -> mm^3; /1000 -> ml.
#define PX_AREA_MM2 0.01626
unsigned long whitePixelsAccum = 0;   // reused for both counting passes
bool countPixelsMode = false;         // true = PNGDraw also counts white px
bool estimateCancelReq = false;       // Back pressed during the estimate scan
double resinUsedMl = 0.0;             // grows while printing
double resinEstimateMl = 0.0;         // filled by estimateResin()

void * myOpen(const char *filename, int32_t *size) {
  myfile = SD.open(filename);
  *size = myfile.size();
  return &myfile;
}

// Close file for PNG library
void myClose(void *handle) {
  if (myfile) myfile.close();
}

// Read from file for PNG library
int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}

// Seek in file for PNG library
int32_t mySeek(PNGFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Draw PNG Scanline
 * Callback function to draw a line of pixels from the PNG decoder to the display.
 */
void PNGDraw(PNGDRAW *pDraw) {
  uint16_t usPixels[320];
  png.getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff); // Convert line to RGB565
  // Count "lit" (white) pixels for resin estimation. A simple luminance
  // test on the unpacked RGB565 channels works fine here since slices are
  // pure black/white. Threshold ~50%.
  for (int x = 0; x < pDraw->iWidth; x++) {
    uint16_t p = usPixels[x];
    uint8_t r = (p >> 11) & 0x1F;
    uint8_t g = (p >> 5) & 0x3F;
    uint8_t b = p & 0x1F;
    // normalize to 0..255-ish and test brightness
    if (((r << 3) + (g << 2) + (b << 3)) / 3 > 128) whitePixelsAccum++;
  }
  if (!countPixelsMode)
    gfx1->draw16bitRGBBitmap(0, pDraw->y + 0, usPixels, pDraw->iWidth, 1);    // Draw to display (skip when only counting)
  else if (digitalRead(buttonBack) == LOW)
    estimateCancelReq = true;   // latch: sampled every line, so a short Back
                                // press during the estimate scan is not lost
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Print Next PNG
 * Loads and displays the next slice image for the current layer.
 */
void print_next_png(){
  FileName = foldersel_long;
  FileName += "/";
  current_layer ++;
  int i = current_layer;
  // If layer height > 0.06, skip images
  // handling different slicing resolutions 
  if(Layer_Height > 0.06)
    i = current_layer * 2 - 1; 
  FileName += i;
  FileName += ".png";
  char NameChar[110];
  FileName.toCharArray(NameChar, 110);
  int rc;
  whitePixelsAccum = 0;
  rc = png.open((const char *)NameChar, myOpen, myClose, myRead, mySeek, PNGDraw);
  if (rc == PNG_SUCCESS) {
    rc = png.decode(NULL, 0);
    png.close();
  }
  // Accumulate cured-resin volume for this layer (ml)
  resinUsedMl += (double)whitePixelsAccum * PX_AREA_MM2 * Layer_Height / 1000.0;
  //entry.close();
  delay(50);  
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Show the resin estimate result and wait for Start or Back.
 */
bool showResinEstimateResult() {
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(12, 22);
  gfx2->print("Resin needed");
  gfx2->setTextColor(0x879F);
  gfx2->setCursor(8, 44);
  gfx2->print(resinEstimateMl, 1);
  gfx2->print(" ml = ");
  gfx2->print(resinEstimateMl / (double)Vat_Capacity_Ml, 1);
  gfx2->print(" VAT");
  gfx2->setTextColor(WHITE);
  uiButtons("Back", "Start", 0x879F);

  while (digitalRead(buttonUp) == LOW) delay(10);
  delay(150);
  while (true) {
    if (digitalRead(buttonOK) == LOW) {
      while (digitalRead(buttonOK) == LOW) delay(10);
      return true;
    }
    if (digitalRead(buttonBack) == LOW) {
      while (digitalRead(buttonBack) == LOW) delay(10);
      return false;
    }
    delay(10);
  }
}

/**
 * @brief Estimate total resin for the selected model by decoding every layer
 * PNG and counting white pixels (no drawing). Shows a progress bar with %.
 * Result in ml -> resinEstimateMl. Then shows the result with Back / Start
 * buttons (same layout as the preview screen) and waits for the user.
 * @return true if the user pressed Start (begin printing), false for Back.
 */
bool estimateResin(){
  double cachedMl = 0;
  if (getModelMetadataResin(String(foldersel_long), cachedMl)) {
    resinEstimateMl = cachedMl;
    return showResinEstimateResult();
  }

  netProgressStart("Estimating resin ml", "");

  int total = layer_counter;              // already halved for 0.1 mm by screen111
  double volMm3 = 0.0;
  countPixelsMode = true;                 // PNGDraw counts, does not draw
  estimateCancelReq = false;

  for (int layer = 1; layer <= total; layer++) {
    // Back cancels a long estimate. The press is latched inside PNGDraw()
    // (sampled every decoded line), so even a short tap registers here.
    if (estimateCancelReq || digitalRead(buttonBack) == LOW) {
      countPixelsMode = false;
      while (digitalRead(buttonBack) == LOW) delay(10);
      return false;                       // caller redraws the preview screen
    }
    int idx = layer;
    if (Layer_Height > 0.06) idx = layer * 2 - 1;
    String fn = String(foldersel_long) + "/" + String(idx) + ".png";
    char nc[110]; fn.toCharArray(nc, 110);
    whitePixelsAccum = 0;
    if (png.open((const char*)nc, myOpen, myClose, myRead, mySeek, PNGDraw) == PNG_SUCCESS) {
      png.decode(NULL, 0);
      png.close();
    }
    volMm3 += (double)whitePixelsAccum * PX_AREA_MM2 * Layer_Height;
    // progress bar + percent
    int w = (int)(136L * layer / total);
    gfx2->fillRect(12, 50, w, 12, ORANGE);
    gfx2->fillRect(60, 30, 60, 14, BLACK);
    gfx2->setCursor(60, 42);
    gfx2->print((int)(100L * layer / total));
    gfx2->print("%");
  }

  countPixelsMode = false;
  resinEstimateMl = volMm3 / 1000.0;
  setModelMetadataResin(String(foldersel_long), resinEstimateMl);
  return showResinEstimateResult();
}
