// ===================================================================================
// Power-loss print resume
// ===================================================================================
// A tiny checkpoint file on the SD card lets the printer pick a print back up
// after a power blip/outage. It is (re)written in place a few times per layer,
// always between motor moves (never inside the exposure timing loop), and
// deleted at the print's single exit point (finish / cancel / homing abort).
//
// Record phases (resumePhase):
//   S  print started, homing not finished - a resume simply restarts the print
//   E  plate stationary at the NEXT layer's exposure height (pos is exact);
//      written after every lower_print(). A loss during the long exposure
//      lands here - the interrupted layer is re-exposed in full (slight
//      over-cure of one layer, no geometry error).
//   M  peel lift/lower in motion - pos is the height BEFORE the lift, the
//      plate is physically somewhere in [pos, pos + lift]. Resume assumes the
//      LOWEST height so a position error can only open a gap upward, never
//      drive the print into the FEP.
//   P  paused, plate parked at pos (exact).
//
// The record is fixed-width (padded numbers, folder last) so the in-place
// rewrite covers the previous one exactly - no truncation needed, one sector.
//
// Resume deliberately does NOT home: the bottom endstop is the only reference
// and homing would crush the half-printed object into the vat. The geared
// 28BYJ-type Z stepper is self-locking, so the plate stays where power died.

static const char *RESUME_PATH = "/tinymaker-resume.txt";

// Write a checkpoint with an explicit position (steps from the print's home).
void resumeCheckpointAt(char phase, long posSteps) {
  char buf[240];
  uint32_t elapsedSecs = (millis() - printStartMs) / 1000UL;
  snprintf(buf, sizeof(buf),
           "TMR1\n"
           "phase=%c\n"
           "layer=%05d\n"
           "total=%05d\n"
           "lh=%03d\n"
           "pos=%+011ld\n"
           "resin=%011.3f\n"
           "elapsed=%010lu\n"
           "uvled=%010lu\n"
           "folder=%s\n",
           phase, current_layer, layer_counter,
           (int)lroundf(Layer_Height * 100),
           posSteps, resinUsedMl,
           (unsigned long)elapsedSecs,
           (unsigned long)(uvLedSessionMs / 1000UL),
           foldersel_long);
  File f = SD.open(RESUME_PATH, FILE_WRITE);
  if (!f) return;
  f.seekSet(0);          // FILE_WRITE opens at end - rewrite from the start
  f.print(buf);
  f.close();
}

void resumeCheckpoint(char phase) {
  resumeCheckpointAt(phase, stepper.currentPosition());
}

// Fresh print start: drop any stale record (a previous print's folder name
// may be longer than this one's) and write the S phase.
void resumeWriteStart() {
  SD.remove((char *)RESUME_PATH);
  resumeCheckpointAt('S', 0);
}

void resumeClear() {
  SD.remove((char *)RESUME_PATH);
}

// --- parse helpers for our own fixed format (mirrors the backup parser) ---
static long resumeNum(const String &j, const char *key, long def) {
  String needle = String(key) + "=";
  int p = j.indexOf(needle);
  return p < 0 ? def : atol(j.c_str() + p + needle.length());
}

static double resumeDbl(const String &j, const char *key, double def) {
  String needle = String(key) + "=";
  int p = j.indexOf(needle);
  return p < 0 ? def : atof(j.c_str() + p + needle.length());
}

/**
 * Load and validate the checkpoint into the resume* globals (TinyMaker.ino).
 * Returns false (and leaves the file alone) on any anomaly: missing file,
 * bad magic, out-of-range values, a layer-height setting that changed since
 * the print (the geometry would no longer match), a vanished model folder or
 * a missing next-layer PNG. Callers only prompt when this returns true.
 */
bool resumeLoad() {
  resumePhase = 0;
  File f = SD.open(RESUME_PATH);
  if (!f) return false;
  String j;
  j.reserve(256);
  while (f.available() && j.length() < 512) j += (char)f.read();
  f.close();

  if (!j.startsWith("TMR1\n")) return false;

  char ph = 0;
  {
    int p = j.indexOf("phase=");
    if (p >= 0 && p + 6 < (int)j.length()) ph = j[p + 6];
  }
  if (ph != 'S' && ph != 'E' && ph != 'M' && ph != 'P') return false;

  long layer = resumeNum(j, "layer", -1);
  long total = resumeNum(j, "total", -1);
  long lh = resumeNum(j, "lh", -1);
  long pos = resumeNum(j, "pos", -1);
  if (total < 1 || total > MAX_LAYER_FILES) return false;
  if (layer < 0 || layer >= total) return false;
  if (ph == 'S' && layer != 0) return false;
  if (lh != lroundf(Layer_Height * 100)) return false;   // setting changed
  if (pos < 0 || pos > (long)(max_height * steps_mm)) return false;

  int p = j.indexOf("folder=");
  if (p < 0) return false;
  p += 7;
  int e = j.indexOf('\n', p);
  if (e < 0) e = j.length();
  String folder = j.substring(p, e);
  if (folder.length() == 0 || folder.length() >= sizeof(resumeFolder))
    return false;

  // The model folder and the next layer's PNG must still be on the card
  File dir = SD.open(folder.c_str());
  if (!dir) return false;
  bool isDir = dir.isDirectory();
  dir.close();
  if (!isDir) return false;

  int nextIdx = layer + 1;
  if (Layer_Height > 0.06) nextIdx = (layer + 1) * 2 - 1;
  String png = folder + "/" + String(nextIdx) + ".png";
  File lf = SD.open(png.c_str());
  if (!lf) return false;
  lf.close();

  resumePhase = ph;
  resumeLayer = (int)layer;
  resumeTotal = (int)total;
  resumePosSteps = pos;
  resumeResinMl = resumeDbl(j, "resin", 0);
  resumeElapsedSecs = (uint32_t)resumeNum(j, "elapsed", 0);
  resumeUvLedSecs = (uint32_t)resumeNum(j, "uvled", 0);
  folder.toCharArray(resumeFolder, sizeof(resumeFolder));
  return true;
}

// Transition_Exposure fast-forward: turn_on_LED() decrements it once per
// transition layer starting from Base_Exposure - replay the decrements for
// the layers that were already cured before the power loss.
float resumeTransitionExposureSeed(int curedLayers) {
  if (curedLayers <= Base_Layer) return Base_Exposure;
  int done = curedLayers - Base_Layer;
  if (done > Transition_Layer) done = Transition_Layer;
  float c = (float)(Base_Exposure - Regular_Exposure) / (float)(Transition_Layer + 1);
  return (float)Base_Exposure - c * done;
}

/**
 * 0-2: third choice on the boot resume prompt (UP) - the user does NOT want
 * to continue the print, but the plate is parked down on a possibly
 * FEP-stuck object and must come up WITHOUT homing (homing would drive it
 * into the vat - the Simon Fell "stuck down, will not raise" case).
 * Trust the checkpoint like a resume would: seed the position ('M' records
 * the pre-lift LOW height, so an error can only lift HIGHER than intended -
 * the safe direction), peel free with the slow lift, then continue up to a
 * pause-style +20mm park and discard the checkpoint. Runs from the boot
 * prompt, before network_setup - plain blocking moves are fine here.
 * A power loss mid-raise leaves the old checkpoint with the plate higher
 * than recorded - the next boot's recovery errs upward, still safe.
 */
void resumeRaisePlateAndDiscard() {
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 44);
  gfx2->print("Raising plate...");

  stepper.setCurrentPosition(resumePosSteps);
  stepper.enableOutputs();

  // Peel the last (possibly half-cured) layer off the FEP at the slow lift
  // speed - the same first move a normal layer cycle or a resume would make.
  long liftSteps = (long)((Slow_Lift_Distance + Fast_Lift_Distance) * steps_mm);
  stepper.setMaxSpeed(Slow_Lift_Feedrate * steps_mm / 60);
  stepper.move(liftSteps);
  while (stepper.distanceToGo() != 0) stepper.run();
  delay(50);

  // Then up to the pause-style park: +20mm over the checkpoint, clamped.
  long maxSteps = (long)(max_height * steps_mm);
  long target = resumePosSteps + (long)(20 * steps_mm);
  if (target > maxSteps) target = maxSteps;
  if (target > stepper.currentPosition()) {
    stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
    stepper.moveTo(target);
    while (stepper.distanceToGo() != 0) stepper.run();
  }
  stepper.disableOutputs();
  delay(200);
  resumeClear();
}

/**
 * Re-establish Z after a power loss WITHOUT homing (the print would hit the
 * vat before the endstop). Trusts the checkpointed position, peels the last
 * (possibly half-cured, FEP-stuck) layer free, and settles the plate at the
 * next layer's exposure height. For 'M' the recorded pre-lift height is the
 * safe LOW assumption - a real position above it only shifts the print up.
 */
void resumeRecoverPosition() {
  long liftSteps = (long)((Slow_Lift_Distance + Fast_Lift_Distance) * steps_mm);
  long layerSteps = (long)(Layer_Height * steps_mm);

  // A second loss DURING recovery must stay fail-safe too: until the plate
  // settles, claim the lowest height involved ('M' semantics) so the next
  // attempt can also only err upward.
  long lowPos = (long)resumeLayer * layerSteps - layerSteps;
  if (lowPos < 0) lowPos = 0;
  resumeCheckpointAt('M', lowPos);

  stepper.setCurrentPosition(resumePosSteps);
  stepper.enableOutputs();

  long target;
  if (resumePhase == 'P') {
    // parked above the print (pause lift already peeled) - just come down
    target = (long)resumeLayer * layerSteps;
  } else {
    // peel first: slow lift, same as the start of a normal layer cycle
    stepper.setMaxSpeed(Slow_Lift_Feedrate * steps_mm / 60);
    stepper.move(liftSteps);
    while (stepper.distanceToGo() != 0) stepper.run();
    delay(50);
    target = (resumePhase == 'M') ? resumePosSteps + layerSteps  // pre-lift pos
                                  : resumePosSteps;              // 'E': same height
  }

  stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
  stepper.moveTo(target);
  while (stepper.distanceToGo() != 0) stepper.run();
  stepper.disableOutputs();
  delay(200);
}
