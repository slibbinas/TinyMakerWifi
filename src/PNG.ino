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

// ===================================================================================
// P-live: per-layer silhouette capture for live 3D in EVERY browser (0.17)
// ===================================================================================
// A layer is already decoded pixel-by-pixel to expose it. On the ~36 layers the
// dashboard samples for its isometric render we fold that same pass into a tiny
// 64x48 1-bit silhouette and keep the growing stack in RAM. Any browser -
// including one opened mid-print, which cannot read layer PNGs off the locked SD
// card - fetches the stack from /api/live/slices and grows the identical 3D.
// The sampling mirrors dashboard.html fetchSlices(): N = min(36, layer_counter),
// slice k = logical layer 1 + round(k*(layer_counter-1)/(N-1)). The buffer lives
// only for the duration of one print (freed at its single exit).
// LIVE_* and the shared globals (liveBuf/liveN/liveCaptured) live in
// TinyMaker.ino - it is concatenated before Network.ino, which must see them to
// serve /api/live/slices (this PNG.ino comes last, after both). The state below
// is used only here, so it stays local.
int liveNextK = 0;        // next slot to fill
int liveNextLayer = 0;    // logical layer that fills liveNextK (0 = none pending)
static int liveSlot = -1; // slot PNGDraw is filling right now (-1 = not capturing)
static int liveSrcW = 0, liveSrcH = 0;  // active layer's source dims, for scaling

// Logical layer (1-based) whose silhouette fills slot k - mirrors the JS sampler.
int liveSampleLayer(int k) {
  if (liveN <= 1) return 1;
  return 1 + (int)lroundf((float)k * (layer_counter - 1) / (float)(liveN - 1));
}

void liveClear() {
#if ENABLE_NETWORK
  if (liveBuf) { free(liveBuf); liveBuf = NULL; }
  liveN = liveCaptured = liveNextK = liveNextLayer = 0;
  liveSlot = -1;
#endif
}

// Called once at print start (total = layer_counter). Allocates the stack; on a
// failed alloc the feature just stays off (liveBuf NULL) and the print is normal.
void liveBegin(int total) {
#if ENABLE_NETWORK
  liveClear();
  if (total < 1) return;
  liveN = total < LIVE_MAX_SLICES ? total : LIVE_MAX_SLICES;
  liveBuf = (uint8_t *)calloc((size_t)liveN, LIVE_SLICE_BYTES);
  if (!liveBuf) { liveN = 0; return; }
  // Skip sample heights at or below the current layer: a resumed print starts at
  // resumeLayer, and those layers are already printed (their PNGs stay unread -
  // no SD mid-print). Fresh print: current_layer is 0, so nothing is skipped and
  // the first target is layer 1. Without this the sampler waits for layer 1
  // forever on resume and captures nothing (audit M1).
  liveNextK = 0;
  while (liveNextK < liveN && liveSampleLayer(liveNextK) <= current_layer) liveNextK++;
  liveNextLayer = liveNextK < liveN ? liveSampleLayer(liveNextK) : 0;
#endif
}

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
#if ENABLE_NETWORK
  // P-live: on a sampled layer, fold this scanline into the 64x48 silhouette.
  // liveSlot >= 0 only inside print_next_png()'s capture window, so the estimate
  // and preview decode passes (liveBuf NULL / liveSlot -1) never touch it.
  bool liveCap = (liveSlot >= 0 && liveBuf && liveSrcW > 0 && liveSrcH > 0);
  uint8_t *liveRow = NULL;
  int liveSy = 0;
  if (liveCap) {
    liveSy = (int)((long)pDraw->y * LIVE_GH / liveSrcH);
    if (liveSy < 0) liveSy = 0; else if (liveSy >= LIVE_GH) liveSy = LIVE_GH - 1;
    liveRow = liveBuf + (size_t)liveSlot * LIVE_SLICE_BYTES;
  }
#endif
  for (int x = 0; x < pDraw->iWidth; x++) {
    uint16_t p = usPixels[x];
    uint8_t r = (p >> 11) & 0x1F;
    uint8_t g = (p >> 5) & 0x3F;
    uint8_t b = p & 0x1F;
    // normalize to 0..255-ish and test brightness
    bool lit = (((r << 3) + (g << 2) + (b << 3)) / 3 > 128);
    if (lit) whitePixelsAccum++;
#if ENABLE_NETWORK
    if (liveCap && lit) {
      int sx = (int)((long)x * LIVE_GW / liveSrcW);
      if (sx < 0) sx = 0; else if (sx >= LIVE_GW) sx = LIVE_GW - 1;
      int cell = liveSy * LIVE_GW + sx;              // silhouette cell (row-major)
      liveRow[cell >> 3] |= (uint8_t)(1 << (cell & 7));   // LSB-first bit packing
    }
#endif
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
#if ENABLE_NETWORK
  // P-live: is this logical layer one of the sampled heights? Decided before the
  // decode; the sampler is advanced AFTER it whether or not the slice was
  // actually captured, so an unreadable sample PNG (or bad dims) can't stall the
  // rest of the print (audit M2). current_layer climbs by 1 per layer, so it
  // hits each target exactly once.
  bool liveSampleNow = (liveBuf && liveNextK < liveN && current_layer == liveNextLayer);
#endif
  rc = png.open((const char *)NameChar, myOpen, myClose, myRead, mySeek, PNGDraw);
  if (rc == PNG_SUCCESS) {
#if ENABLE_NETWORK
    liveSlot = -1;
    if (liveSampleNow) {
      liveSrcW = png.getWidth();
      liveSrcH = png.getHeight();
      if (liveSrcW > 0 && liveSrcH > 0) {
        liveSlot = liveNextK;
        memset(liveBuf + (size_t)liveSlot * LIVE_SLICE_BYTES, 0, LIVE_SLICE_BYTES);
      }
    }
#endif
    rc = png.decode(NULL, 0);
    png.close();
#if ENABLE_NETWORK
    if (liveSlot >= 0 && liveSlot + 1 > liveCaptured)
      liveCaptured = liveSlot + 1;   // publish the freshly filled slot
    liveSlot = -1;
#endif
  }
#if ENABLE_NETWORK
  if (liveSampleNow) {   // advance past this sample even if the capture was skipped
    liveNextK++;
    liveNextLayer = liveNextK < liveN ? liveSampleLayer(liveNextK) : 0;
  }
#endif
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
