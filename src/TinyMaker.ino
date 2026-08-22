/**
 * @file TinyMaker-Firmware-v1-0-2.ino
 * @author Tinymaker Team (Original), Viktoras Sidlauskas (Modified)
 * @version 1.0.2-vs-wifi-0.1
 * @date 2027-07-26
 * @brief Main firmware for Tinymaker MSLA 3D Printer.
 *
 * board ESP32-WROOM-32E-N4
 * Modifications by Viktoras Sidlauskas slibbinas@gmail.com
 *
 * This file handles the entire print process, UI interaction, motor control, and UV exposure logic. 
 *
 * changes
 * - Added detailed comments (EN)
 * - Removed dead code
 * - Code organization and cleanup
 * - Corrected typo: "Maintenance" of Screen 2: Main Menu
 */

 // ===================================================================================
// Build switches - set to 1/0 and recompile
// ===================================================================================
#define ENABLE_NETWORK       1   // 0 = firmware be WiFi/upload (kaip originalas)
#define ENABLE_SERIAL_DEBUG  0   // 0 = jokio Serial isvesties

#if ENABLE_SERIAL_DEBUG
  #define DBG    Serial.printf
  #define DBGLN(x)  Serial.println(x)
#else
  static inline void DBG(const char *, ...) {}
  #define DBGLN(x)
#endif

// Maksimalus modelio PNG failu skaicius: N x 0.05 mm auksciui.
// Originalas: 1080 (=54 mm). Pakelta iki 1200 (=60 mm) - realus sio
// spausdintuvo limitas; Z eigos atsarga kelimui lieka (max_height = 68 mm).
#define MAX_LAYER_FILES 1200

#include <SPI.h>
#include <EEPROM.h>              // For storing settings persistently
#include <AccelStepper.h>        // Stepper motor control library
#include <Arduino_GFX_Library.h> // Graphics library for driving displays
#include "FreeSans8pt7b.h"       // Custom font
#include <PNGdec.h>              // PNG decoder library for reading print layers
#include <SdFat.h>               // SD card file system library
#include <esp_system.h>          // hardware random for boot-animation shuffle
#include "ModelImport.h"         // Shared ZIP import result/option structs

#if ENABLE_NETWORK
#include <WiFi.h>
#include <WiFiManager.h>   // tzapu, v2.0.x
#include <ESPmDNS.h>
#include <WebServer.h>
#include <unzipLIB.h>      // bitbank2
#include <Update.h>        // web /update firmware flashing
#include <ArduinoOTA.h>    // PlatformIO espota uploads
#include <esp_wifi.h>      // reliable WiFi credential erase / config read
#endif
#include <Preferences.h>   // NVS: forcePortal flag, total print hours
                           // (outside the network guard - print hours are
                           // tracked in network-free builds too)

// Resin estimation globals live in PNG.ino - forward-declare for the
// files compiled before it (Interface.ino, TinyMaker.ino)
extern double resinUsedMl;
extern double resinEstimateMl;

// --- Resin volume math (R-cal 0.17): ONE definition for the whole build ---
// One masking-LCD pixel area: 40.8 x 30.6 mm / (320 x 240) = 0.01626 mm^2 (from
// the PrusaSlicer TinyMaker profile). Volume = whitePixels * PX_AREA_MM2 *
// layerHeight (mm) -> mm^3; /1000 -> ml.
// This lived in PNG.ino, which is concatenated AFTER Network.ino - so
// estimateModelResin() carried a duplicated bare 0.01626 literal. Declared here
// (TinyMaker.ino is prepended first) every estimate site shares one formula,
// which is also the single place the R-cal factor hooks into.
#define PX_AREA_MM2 0.01626
// Resin profiles (ResinProfile.ino) - declared here because Interface.ino and
// Network.ino come before it in the .ino concatenation order.
#define RESIN_MAX_PROFILES 16
// A profile is the whole print recipe: everything that decides how the print
// comes out. What stays outside describes the MACHINE and changes nothing about
// the result - VAT size and empty weight, the low-resin warning levels, "ask
// about refill", the pause inspection lift and the screen sleep.
//
// Layer height is IN (V 08-16). Exposure without it is an incomplete recipe -
// 0.05 mm needs less light than 0.10 mm - so two profiles for one resin is the
// normal case, exactly like a slicer's "material x layer height" rows. It also
// makes the flat-print mismatch LESS likely, not more: today the printer's
// height and the slicer profile are matched by hand in two separate places,
// which is how that bug happened at all. LH-chk still checks the file itself.
struct ResinProfileValues {
  float layerHeight;   // mm - only 0.05 or 0.10 exist on this machine
  long baseExposure;   // whole seconds (EEPROM addr 2)
  long regularDs;      // deciseconds (0.17 0-3)
  uint8_t baseLayers;
  uint8_t transitionLayers;
  uint8_t slowLiftDist, fastLiftDist;      // mm
  int slowLiftFeed, fastLiftFeed, dropBackFeed;   // mm/min
  float density;       // g/ml
  float calFactor;     // R-cal slope
  float fixedMl;       // R-cal per-print offset (ml)
  // The weighed samples travel WITH the resin. They are grams measured against
  // one particular resin, so leaving them on the machine would let the next
  // weighing fit a line through two different resins - and would make the
  // dashboard call a never-weighed resin "calibrated". -1 = no sample.
  float calRawA, calGramsA, calRawB, calGramsB;
};
// Provenance travels WITH the profile, not just in the gh-pages catalogue: once
// installed, a profile still has to be able to say who tested it and where to
// buy it, otherwise the badge disappears exactly when the resin starts being
// used (V 08-16). Empty = unknown, which is the normal case for a profile the
// user saved themselves.
struct ResinProfileMeta {
  String testedBy;     // who printed with it - "" for a self-made profile
  String testedOn;     // when, free text ("2026-08")
  String buyUrl;       // affiliate/redirect link, shown only when present
};
// One read of the profile file answers everything the list needs.
struct ResinProfileInfo {
  ResinProfileValues v;
  ResinProfileMeta meta;
  String display;
  bool builtin;
  bool edited;         // a built-in whose numbers differ from the flash ones
  bool hasFile;        // an overlay file exists on the card (may equal factory)
};
void resinProfileFromCurrent(ResinProfileValues &v);
bool resinProfileValues(const String &name, ResinProfileValues &v);
bool resinProfileInfo(const String &name, ResinProfileInfo &info);
String resinProfilePath(const String &name);
int listResinProfiles(String out[], int maxN);
bool resinProfileExists(const String &name);
bool applyResinProfile(const String &name);
void publishStopEstimate();   // Motor.ino - stabdymo laukimo ivertis
bool writeResinProfile(const String &name, const String &display);
bool writeResinProfileValues(const String &name, const String &display,
                             const ResinProfileValues &vals, const ResinProfileMeta &meta);
bool deleteResinProfile(const String &name);
String nextResinProfile(const String &current);
String sanitizeSlug(const String &in, const char *fallback = "downloaded");
int resinBuiltinIndex(const String &name);
bool resinProfileFileExists(const String &name);

inline double pxToMlRaw(unsigned long px, float layerH) {
  return (double)px * PX_AREA_MM2 * layerH / 1000.0;   // RAW - no calibration
}
bool estimateResin();               // returns true if user chose Start
bool startFromResin = false;        // set when Start pressed on resin screen
// The layer height in force when the staged model counted its layers. A resin
// profile carries its own height, so a switch from the browser between staging
// and Start would otherwise print with the old count - half the model at
// 0.10 -> 0.05, or past the last slice the other way (V asked, 08-16).
float stagedLayerHeight = -1;
bool webStartPrint = false;         // set by the web SD manager after preview validation
bool webResumePrint = false;        // set by the web dashboard while paused

// Deferred SD jobs (1-32/1-33): a long delete or model import queued by an HTTP
// handler and executed from loop() (sdJobRun in Network.ino). The single-
// threaded WebServer cannot answer other clients from inside a handler, so
// running the work there froze every other browser for the whole operation;
// from loop() context the job's inner loops can service HTTP (sdJobService).
// sdJobKind doubles as the busy gate: printerBusy() is true while it is set,
// so every existing "printer busy" rejection protects the SD job for free.
// The printer's own menu delete/import set these too - same protection, and
// the dashboards see what the printer is doing instead of timeouts.
String sdJobKind = "";              // "" | "delete" | "import"
String sdJobName = "";              // model the job works on (shown in /api/status)
String sdJobZipPath = "";           // import: uploaded archive waiting to be unpacked
ModelImportOptions sdJobImportOptions;  // import: options captured from the upload request
bool sdJobRunning = false;          // true while the job body executes (enables servicing)
// SD-prog: how far the job is. The printer's own screen has shown this all
// along ("Unpacking layers 120/240", the delete bar); the numbers simply never
// reached /api/status, so every dashboard sat on a mute "Importing model" for
// the whole minute. Defined here, not in Network.ino, because Folder.ino writes
// them and precedes Network.ino in the .ino concatenation.
int sdJobDone = 0, sdJobTotal = 0;  // 0/0 = no count available for this job
// 0-28: SD content revision - bumped after any unpack/delete/import so every
// dashboard reloads its SD list. Defined here (not Network.ino) because
// Folder.ino bumps it too and precedes Network.ino in the .ino concatenation.
uint32_t sdRev = 0;

// --- Power-loss resume (checkpoint file logic in Resume.ino) ---
// Data parsed from /tinymaker-resume.txt by resumeLoad(); defined here so the
// print-start code in this (first) file can see them.
bool resumeStartPrint = false;      // boot resume prompt accepted -> start path
bool resumeBootPending = false;     // suppresses the boot-update prompt
bool powerRestoreNotifyPending = false; // 0.17: send one "power restored" push once WiFi is up this boot
bool networkStarted = false;        // network_setup ran (it is idempotent via this)
bool mdnsAnnounced = false;         // MDNS.begin() succeeded; false after an
                                    // offline boot, so the network_loop
                                    // watchdog knows it still owes the announce
// 0-33: the dashboard's answer to the boot resume prompt - set by the
// /api/resume/* handlers, consumed in loop() while screen 427 is up.
// 'R' resume, 'L' lift plate + discard, 'D' discard. Deferred to loop()
// because lift moves the motor - never inside an HTTP handler.
char webResumeAction = 0;
char resumePhase = 0;               // 'S' start, 'E' exposing, 'M' moving, 'P' paused
int resumeLayer = 0;                // fully cured layers at the checkpoint
int resumeTotal = 0;                // total print layers
long resumePosSteps = 0;            // stepper position at the checkpoint
double resumeResinMl = 0;           // resinUsedMl at the checkpoint
uint32_t resumeElapsedSecs = 0;     // print time elapsed at the checkpoint
uint32_t resumeUvLedSecs = 0;       // uvLedSessionMs (as secs) at the checkpoint
char resumeFolder[101] = "";        // model folder of the interrupted print
// Layer height of the interrupted print, in hundredths of a mm. resumeLoad()
// checks it at boot, and the recovery move is computed from the height in force
// when Resume is finally pressed - so it is checked again there. Every route
// that could move the height in between now answers 409 while the prompt
// stands; this is the belt behind those braces (audit 08-16).
int resumeLayerHeightCm = -1;

// Print-list selection kind: false = model folder (OK prints), true =
// .sl1/.zip archive in the SD root (OK imports/converts it). Maintained by
// listEntryValid() in Folder.ino.
bool selIsArchive = false;

// --- VAT resin bookkeeping (no sensor - estimate only) ---
// vatRemainingMl counts down from "VAT refilled" by each layer's cured-volume
// estimate. -1 = never set; lazily seeded to Vat_Capacity_Ml (see vatRemaining()).
float vatRemainingMl = -1;
bool lowResinPauseEnabled = false;  // pause between layers when estimate runs low
uint8_t lowResinThresholdMl = 2;    // 0.17 #40: STOP level (ml, 1..3) - pause/stop trigger; also pre-start check
uint8_t lowResinWarnMl = 5;         // 0.17 #40: WARN level (ml, 3..15) - warns (keeps printing), independent of the stop checkbox
bool lowResinNotified = false;      // latch: pause fires once per threshold crossing
bool lowResinPreWarned = false;     // 0.17 #40: latch - one-shot warning per print (re-armed on refill)
bool resinWarnAccepted = false;     // pre-start low-resin warning acknowledged
double resinSampledMl = 0;          // resinUsedMl already subtracted from the VAT
bool askRefillEnabled = true;       // ask "VAT refilled?" before every print
bool previewFlip = false;           // dashboard 3D preview upside down (a viewing
                                    // preference - lives in printer config so it
                                    // holds across every browser/phone)
bool refillAsked = false;           // the ask was answered for this start attempt
float resinNeedForModelMl = -1;     // fresh full-model estimate for the selected
                                    // model (-1 = none); set by the resin screen,
                                    // cleared when a new preview opens

// --- R-cal (0.17): white-pixel -> ml correction measured against a scale ---
// The geometric estimate ignores what really leaves the vat: resin clinging to
// the plate, dripping off during the lift, cured supports, over-cure bloom. One
// weighed print fixes all of it at once: factor = measured_ml / raw_estimate_ml.
// Applied at every display/accumulation point (model.json keeps the RAW value,
// so re-calibrating updates already-scanned models too).
// Two physically different errors, so two numbers (V 08-09):
//   used_ml = raw_geometric_ml * resinCalFactor + resinFixedMl
// * resinCalFactor scales with the model - it corrects the GEOMETRY estimate
//   (pixel area, layer height, over-cure bloom).
// * resinFixedMl is per-print and size-independent - the film that coats the
//   plate and drips off when it comes out. Multiplying it by the geometry
//   correction would be meaningless, hence + and not *.
// One weighed print cannot separate a slope from an offset, so calibration
// keeps TWO samples of clearly different size and solves the line through them.
#define RESIN_DENSITY_DEF 1.1f      // SUNLU spec 1.06-1.16; measurable, see below
#define RESIN_CAL_MIN 0.5f
#define RESIN_CAL_MAX 2.0f
#define RESIN_FIXED_MAX 10.0f       // ml of plate film - more than this is a typo
float resinCalFactor = 1.0f;        // NVS "resinCal"  - slope, 1.0 = uncalibrated
float resinFixedMl   = 0.0f;        // NVS "resinFixed" - per-print offset (ml)
float resinDensity   = RESIN_DENSITY_DEF;  // NVS "resinDens" - g/ml, weigh a
                                    // known syringe volume to make grams exact
// Calibration samples: A = the smaller print, B = the larger one (-1 = empty).
// calMeas* hold the GRAMS the scale showed - not ml. Grams are what was actually
// measured; ml is derived. Storing ml froze each sample to the density in force at
// entry, so editing the density between the two prints silently mixed bases (V
// found this 08-09). With grams, a density change simply re-fits both points.
float calRawA = -1, calMeasA = -1, calRawB = -1, calMeasB = -1;
float calNewRaw = -1, calNewMeas = -1;   // RAM: the sample entered most recently
float lastPrintRawMl = -1;          // NVS "lastPrintMl": RAW ml of the last print
                                    // (-1 = none yet) - the calibration reference
double resinUsedRawMl = 0.0;        // RAM twin of resinUsedMl, WITHOUT the factor

// 0.17 0-16: the resin profile in force. Only the slug is stored - the values
// themselves live in the ordinary settings, because applying a profile just
// copies them there (see ResinProfile.ino). "" = none picked yet.
// 0.17 SL-mod: whether the slicer module is live. Deliberately a PRINTER
// setting, not a web lookup - it has to work with no internet, and switching it
// must not need a firmware release or a git push. No UI writes it; the slicer
// module owns it (POST /api/config slicer_on=1). Off until it says otherwise.
bool slicerModuleOn = false;

String resinProfileName = "";
// Bumped whenever a profile is applied, written or deleted, so the LCD menu
// knows when its cached label went stale (0.17 0-16).
uint32_t resinProfileRev = 0;
// Weight of the empty vat (g). One moulded part, one tool, no custom vats -
// so this is a constant of the machine, not something to ask the user for
// (V 08-16). Density is the number that actually varies, and that one lives in
// the resin profile. Weighing the vat then gives the remaining ml exactly,
// instead of trusting the marker.
#define VAT_EMPTY_G_DEF 56.56f   // measured on the reference printer, 08-09
#define VAT_EMPTY_G_MAX 500.0f
float vatEmptyG = VAT_EMPTY_G_DEF;

// Factory settings reset - shared by setup() (bad/blank EEPROM) and the
// Settings -> "Back to Default" menu (Interface.ino).
void resetSettingsToDefault();
void cleanupManagedSdTemps();


// Total print time, persisted in NVS (survives firmware re-flash, unlike the
// EEPROM settings area). Written rarely - only at print end/cancel - to spare
// flash wear. A power loss mid-print loses that session's time (accepted).
Preferences sysPrefs;
uint32_t totalPrintSecs = 0;        // lifetime printing seconds (loaded in setup)
uint32_t totalUvLedSecs = 0;        // lifetime UV LED on-time seconds - the LED ages by
                                    // lit time, not print time (dry runs don't count)
unsigned long uvLedSessionMs = 0;   // this print's LED-on ms, folded in at savePrintTime
unsigned long printStartMs = 0;     // millis() when the current print started
// Live phase countdown for the dashboard ("Curing · 9s"): when the current
// phase began and how long it should run. Curing is exact - the exposure
// computed for this layer. Lifting/dropping use the previous layer's measured
// duration: layers repeat almost perfectly, so the last cycle is the best
// predictor the firmware has. Total 0 = unknown (first layer, pause, cancel).
unsigned long phaseStartMs = 0;
unsigned long phaseTotalMs = 0;
unsigned long prevLiftMs = 0, prevDropMs = 0;
// 0.17 (V 08-18): stabdymas ir pauze zmogui nera vienas laukimas, o du - pirma
// baigiamas tai, kas jau vyksta, paskui juda plokste. Kiekvienas etapas turi savo
// saziininga trukme, tad pultui reikia zinoti, KURIS is ju bega: pranesimas lieka
// tas pats, persirašo tik tekstas, o juostele pradedama is naujo tik NAUJAM etapui.
// "" = eiline sluoksnio faze (Curing/Lifting/Dropping). Kitos reiksmes:
//   stopTail   - dabaigiamas judesys, kuris vyko stabdymo akimirka
//   stopLift   - galutinis plokstes pakelimas po stabdymo
//   homingBack - homing'as nutrauktas, plokste grizta i nuli
//   pauseWork  - pauze laukia sluoksnio pabaigos
//   pauseLift  - pauzes pakelimas apziurai (pauseLiftMm)
//   resume     - grizimas zemyn tesiant
const char *phaseWaitStage = "";
uint16_t uiTimeoutSecs = 60;        // 0 = never blank the UI screen (default 60 s so the
                                    // screen saver works out of the box - 0-23)
bool uvLedEnabled = true;           // false = dry-run motion/display only
bool wifiEnabled = true;
bool webDashboardEnabled = true;
bool bootUpdateCheckEnabled = true;
bool resumeEnabled = true;          // 0-34: false = never checkpoint / never offer power-loss resume
bool resumePrecise = false;         // 0.17 1-38b: false = Balanced cadence, true = Precise (finer resume, more SD writes)
long resumeLiveSteps = 0;           // 0.17: physical relabel height loaded from a checkpoint (== base for legacy records)
long resumeCycleBaseSteps = 0;      // 0.17: exact pre-lift base of the current layer cycle (drift-free target reference)
int pauseLiftMm = 20;               // 0.17 #82: Pause plate-lift height for inspection (mm, 20-40; runtime-clamped to headroom)
// Granular-checkpoint cadence during the ~9s lift/drop motion (0.17 1-38b).
// Defined here (the first-concatenated TU) so Motor.ino - concatenated before
// Resume.ino - can see them.
#define RESUME_CKPT_MS_BALANCED 800
#define RESUME_CKPT_MS_PRECISE  400
String bootAnimName = "";      // "" = built-in splash; else a basename in /bootanim/
bool wifiTemporarilyEnabled = false;
bool webDashboardTemporarilyEnabled = false;
bool mqttEnabled = false;           // Smart Home / MQTT integration scaffold
String mqttHost = "";
uint16_t mqttPort = 1883;
String mqttUser = "";
String mqttPass = "";
String mqttTopic = "TinyMaker";
bool connectEnabled = false;        // TinyMaker Connect web-service integration
String connectBaseUrl = "https://connect.tinymakerwifi.com";
String connectPrinterName = "";
bool connectLeaderboardOptIn = false;
String connectPrinterPublicId = "";
String connectPublishToken = "";
String connectRecoveryCode = "";
String connectLastStatus = "";
bool connectAutoBackup = false;
uint32_t connectBackupEpoch = 0;
bool tgEnabled = false;             // Telegram outbound notifications (V1)
String tgToken = "";                // bot token (secret - never echoed to browser)
String tgChat = "";                 // chat id to notify
bool waEnabled = false;             // WhatsApp notifications via CallMeBot (one channel at a time)
String waPhone = "";                // phone with country code
String waApiKey = "";               // CallMeBot key (secret - never echoed to browser)
bool dcEnabled = false;             // Discord notifications via a channel webhook
String dcWebhook = "";              // webhook URL (secret - never echoed to browser)
bool statsPingEnabled = true;       // anonymous install ping (MAC hash + version + print hours)
uint16_t prevRegularExposure = 0;   // last replaced Regular exposure in DECISECONDS (0 = none) - dashboard Undo
uint8_t  prevBaseExposure = 0;      // last replaced Base exposure in SECONDS (0 = none) - dashboard Undo
unsigned long lastUiActivityMs = 0;
bool uiBlanked = false;
uint8_t uiSaverPos = 0;               // 0-21 idle screen saver: which of the 5 spots
unsigned long uiSaverLastMoveMs = 0;  // last time the idle text drifted
bool uiDimmedPrint = false;           // 0-22: print screen dimmed into the saver
// A press during a MOVE phase reaches the move-loop button handlers, which
// have no dim awareness - they used to scribble selection outlines onto the
// black saver screen (user finding 07-22: "white squares around play/pause").
// The drawing functions now queue a wake instead; the saver honours it at the
// next safe point (the curing loop's tick - waking mid-move would stall the
// stepper for the ~100 ms full redraw).
bool uiSaverWakeQueued = false;

void savePrintTime() {
  totalPrintSecs += (millis() - printStartMs) / 1000UL;
  totalUvLedSecs += uvLedSessionMs / 1000UL;
  uvLedSessionMs = 0;
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putULong("printSecs", totalPrintSecs);
  sysPrefs.putULong("uvLedSecs", totalUvLedSecs);
  sysPrefs.end();
}

// One-off write for LED time outside prints (Clean Resin Vat exposure).
void saveUvLedTime() {
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putULong("uvLedSecs", totalUvLedSecs);
  sysPrefs.end();
}

void loadDeviceConfig() {
  sysPrefs.begin("tinymaker", true);
  totalPrintSecs = sysPrefs.getULong("printSecs", 0);
  totalUvLedSecs = sysPrefs.getULong("uvLedSecs", 0);
  // Default 60 s (0-23): fresh installs get the screen saver out of the box.
  // Anyone who explicitly chose Off has "uiTimeout" = 0 saved and keeps it.
  uiTimeoutSecs = sysPrefs.getUShort("uiTimeout", 60);
  uvLedEnabled = sysPrefs.getBool("uvLed", true);
  wifiEnabled = sysPrefs.getBool("wifiEnabled", true);
  webDashboardEnabled = sysPrefs.getBool("webDash", true);
  bootUpdateCheckEnabled = sysPrefs.getBool("bootUpdChk", true);
  resumeEnabled = sysPrefs.getBool("resumeEn", true);
  resumePrecise = sysPrefs.getBool("resumePrec", false);
  pauseLiftMm = sysPrefs.getUChar("pauseLift", 20);   // 0.17 #82
  if (pauseLiftMm < 20 || pauseLiftMm > 40) pauseLiftMm = 20;   // clamp legacy/garbage
  statsPingEnabled = sysPrefs.getBool("statsPing", true);
  prevRegularExposure = sysPrefs.getUShort("prevRegDs", 0);   // 0.17 0-3: deciseconds (new key; old UChar prevRegExp abandoned)
  prevBaseExposure = sysPrefs.getUChar("prevBaseS", 0);       // sveikos sekundes
  bootAnimName = sysPrefs.getString("bootAnimName", "");
  mqttEnabled = sysPrefs.getBool("mqttEnabled", false);
  mqttHost = sysPrefs.getString("mqttHost", "");
  mqttPort = sysPrefs.getUShort("mqttPort", 1883);
  mqttUser = sysPrefs.getString("mqttUser", "");
  mqttPass = sysPrefs.getString("mqttPass", "");
  mqttTopic = sysPrefs.getString("mqttTopic", "TinyMaker");
  connectEnabled = sysPrefs.getBool("tmcEnabled", false);
  connectBaseUrl = sysPrefs.getString("tmcUrl", "https://connect.tinymakerwifi.com");
  if (connectBaseUrl == "https://tinymaker.inductie.nu") {
    connectBaseUrl = "https://connect.tinymakerwifi.com";
  }
  connectPrinterName = sysPrefs.getString("tmcName", "");
  connectLeaderboardOptIn = sysPrefs.getBool("tmcLeaderboard", false);
  connectPrinterPublicId = sysPrefs.getString("tmcPublicId", "");
  connectPublishToken = sysPrefs.getString("tmcToken", "");
  connectRecoveryCode = sysPrefs.getString("tmcRecovery", "");
  connectAutoBackup = sysPrefs.getBool("tmcAutoBk", false);
  connectBackupEpoch = sysPrefs.getULong("tmcBkEpoch", 0);
  tgEnabled = sysPrefs.getBool("tgEnabled", false);
  tgToken = sysPrefs.getString("tgToken", "");
  tgChat = sysPrefs.getString("tgChat", "");
  waEnabled = sysPrefs.getBool("waEnabled", false);
  waPhone = sysPrefs.getString("waPhone", "");
  waApiKey = sysPrefs.getString("waApiKey", "");
  dcEnabled = sysPrefs.getBool("dcEnabled", false);
  dcWebhook = sysPrefs.getString("dcWebhook", "");
  if (tgEnabled) { waEnabled = false; dcEnabled = false; }  // one channel at a time
  else if (waEnabled) dcEnabled = false;
  vatRemainingMl = sysPrefs.getFloat("vatRemMl", -1);
  lowResinPauseEnabled = sysPrefs.getBool("lowResinOn", false);
  lowResinThresholdMl = sysPrefs.getUChar("lowResinMl", 2);
  if (lowResinThresholdMl < 1 || lowResinThresholdMl > 3)
    lowResinThresholdMl = 3;  // range shrank to 1..3 in 0.12.2 - clamp old values
  lowResinWarnMl = sysPrefs.getUChar("lowResinWarn", 5);   // 0.17 #40: WARN level
  if (lowResinWarnMl < 3 || lowResinWarnMl > 15) lowResinWarnMl = 5;
  // R-cal: a corrupt/absurd factor would silently distort every resin number -
  // clamp on load, exactly like the low-resin ranges above.
  resinCalFactor = sysPrefs.getFloat("resinCal", 1.0f);
  if (!(resinCalFactor >= RESIN_CAL_MIN && resinCalFactor <= RESIN_CAL_MAX))
    resinCalFactor = 1.0f;          // also catches NaN
  resinFixedMl = sysPrefs.getFloat("resinFixed", 0.0f);
  if (!(resinFixedMl >= 0.0f && resinFixedMl <= RESIN_FIXED_MAX)) resinFixedMl = 0.0f;
  resinDensity = sysPrefs.getFloat("resinDens", RESIN_DENSITY_DEF);
  if (!(resinDensity >= 0.8f && resinDensity <= 2.0f)) resinDensity = RESIN_DENSITY_DEF;
  slicerModuleOn = sysPrefs.getBool("slicerOn", false);   // 0.17 SL-mod
  /* Rakto NERA (svarus NVS) -> „slow", ir tai tiesa: EEPROM tada tikrai laiko
     gamyklinius skaicius, o „slow" butent jie ir yra.
     Raktas YRA, bet tuscias -> paliekam tuscia. Anksciau cia stovejo prievarta
     i „slow" reiksmiu NEPRITAIKIUS, tad istrynus aktyvu profili po perkrovimo
     masina sakydavo „Slow resin (factory)", o suktusi istrinto profilio
     skaiciais. Tuscia busena buvo bent sazininga; ta - ne. Dabar tuscias vardas
     ka nors reiskia: spausdinti neleidziama, kol derva nepasirinkta (V, 08-17). */
  resinProfileName = sysPrefs.getString("resinProf", "slow");   // 0.17 0-16
  vatEmptyG = sysPrefs.getFloat("vatEmptyG", VAT_EMPTY_G_DEF);
  if (!(vatEmptyG > 0.0f && vatEmptyG <= VAT_EMPTY_G_MAX)) vatEmptyG = VAT_EMPTY_G_DEF;
  calRawA  = sysPrefs.getFloat("calRawA", -1);  calMeasA = sysPrefs.getFloat("calMeasA", -1);
  calRawB  = sysPrefs.getFloat("calRawB", -1);  calMeasB = sysPrefs.getFloat("calMeasB", -1);
  // A NaN here would print as a bare nan in /api/config and break the whole JSON.
  if (!(calRawA > 0 && calMeasA > 0)) { calRawA = calMeasA = -1; }
  if (!(calRawB > 0 && calMeasB > 0)) { calRawB = calMeasB = -1; }
  // One-time migration: samples used to be stored in ml. Converting is exact -
  // those ml were produced by dividing the very same grams by this same density.
  if (sysPrefs.getUChar("calUnit", 0) != 1) {
    if (calMeasA > 0) calMeasA *= resinDensity;
    if (calMeasB > 0) calMeasB *= resinDensity;
    sysPrefs.end();
    sysPrefs.begin("tinymaker", false);
    sysPrefs.putFloat("calMeasA", calMeasA);
    sysPrefs.putFloat("calMeasB", calMeasB);
    sysPrefs.putUChar("calUnit", 1);   // 1 = grams
  }
  lastPrintRawMl = sysPrefs.getFloat("lastPrintMl", -1);
  if (!(lastPrintRawMl > 0)) lastPrintRawMl = -1;   // NaN/garbage -> "no print yet"
  askRefillEnabled = sysPrefs.getBool("askRefill", true);
  previewFlip = sysPrefs.getBool("prevFlip", false);
  sysPrefs.end();
}

void saveDeviceConfig() {
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putUShort("uiTimeout", uiTimeoutSecs);
  sysPrefs.putBool("uvLed", uvLedEnabled);
  sysPrefs.putBool("wifiEnabled", wifiEnabled);
  sysPrefs.putBool("webDash", webDashboardEnabled);
  sysPrefs.putBool("bootUpdChk", bootUpdateCheckEnabled);
  sysPrefs.putBool("resumeEn", resumeEnabled);
  sysPrefs.putBool("resumePrec", resumePrecise);
  sysPrefs.putUChar("pauseLift", (uint8_t)pauseLiftMm);   // 0.17 #82
  sysPrefs.putBool("statsPing", statsPingEnabled);
  sysPrefs.putString("bootAnimName", bootAnimName);
  sysPrefs.putBool("mqttEnabled", mqttEnabled);
  sysPrefs.putString("mqttHost", mqttHost);
  sysPrefs.putUShort("mqttPort", mqttPort);
  sysPrefs.putString("mqttUser", mqttUser);
  sysPrefs.putString("mqttPass", mqttPass);
  sysPrefs.putString("mqttTopic", mqttTopic);
  sysPrefs.putBool("tmcEnabled", connectEnabled);
  sysPrefs.putString("tmcUrl", connectBaseUrl);
  sysPrefs.putString("tmcName", connectPrinterName);
  sysPrefs.putBool("tmcLeaderboard", connectLeaderboardOptIn);
  sysPrefs.putString("tmcPublicId", connectPrinterPublicId);
  sysPrefs.putString("tmcToken", connectPublishToken);
  sysPrefs.putString("tmcRecovery", connectRecoveryCode);
  sysPrefs.putBool("tmcAutoBk", connectAutoBackup);
  sysPrefs.putULong("tmcBkEpoch", connectBackupEpoch);
  sysPrefs.putBool("tgEnabled", tgEnabled);
  sysPrefs.putString("tgToken", tgToken);
  sysPrefs.putString("tgChat", tgChat);
  sysPrefs.putBool("waEnabled", waEnabled);
  sysPrefs.putString("waPhone", waPhone);
  sysPrefs.putString("waApiKey", waApiKey);
  sysPrefs.putBool("dcEnabled", dcEnabled);
  sysPrefs.putString("dcWebhook", dcWebhook);
  sysPrefs.putBool("lowResinOn", lowResinPauseEnabled);
  sysPrefs.putUChar("lowResinMl", lowResinThresholdMl);
  sysPrefs.putUChar("lowResinWarn", lowResinWarnMl);   // 0.17 #40
  sysPrefs.putFloat("resinCal", resinCalFactor);       // R-cal 0.17
  sysPrefs.putFloat("resinFixed", resinFixedMl);
  sysPrefs.putFloat("resinDens", resinDensity);
  sysPrefs.putBool("slicerOn", slicerModuleOn);        // 0.17 SL-mod
  sysPrefs.putString("resinProf", resinProfileName);   // 0.17 0-16
  sysPrefs.putFloat("vatEmptyG", vatEmptyG);
  sysPrefs.putFloat("calRawA", calRawA);   sysPrefs.putFloat("calMeasA", calMeasA);
  sysPrefs.putFloat("calRawB", calRawB);   sysPrefs.putFloat("calMeasB", calMeasB);
  sysPrefs.putUChar("calUnit", 1);         // calMeas* = grams
  sysPrefs.putBool("askRefill", askRefillEnabled);
  sysPrefs.putBool("prevFlip", previewFlip);
  sysPrefs.end();
}

// vatRemainingMl is persisted separately: it changes during printing (periodic
// checkpoints) and on "VAT refilled", not with the rest of the config.
void saveVatRemaining() {
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putFloat("vatRemMl", vatRemainingMl);
  // R-cal: the raw twin rides along on the same periodic checkpoint, so a
  // resume restores it directly instead of dividing by whatever factor is in
  // force now (which may differ from the one used before the power cut).
  // Only while printing: this helper is also called from "VAT refilled" and
  // backup restore, and a refill pressed BEFORE resuming would zero the
  // waiting checkpoint (auditor find, 08-11).
  if (printerBusy()) sysPrefs.putFloat("printRawMl", (float)resinUsedRawMl);
  sysPrefs.end();
}

// R-cal: remember what the printer THOUGHT this print used, uncalibrated. The
// user weighs the vat before/after and posts the grams; the factor is then
// simply measured_ml / lastPrintRawMl - no compounding with the current factor.
// Written at the single print exit (finish, cancel and homing-abort all pass
// there); a canceled print is still valid calibration data, since the scale and
// the estimate describe the same partial print.
void saveLastPrintRaw() {
  if (!(resinUsedRawMl > 0.0)) return;      // nothing printed - keep the old one
  if (!uvLedEnabled) return;                // dry run cures nothing: the scale
                                            // would see ~0 g and poison the fit
  lastPrintRawMl = (float)resinUsedRawMl;
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putFloat("lastPrintMl", lastPrintRawMl);
  sysPrefs.end();
}

extern long Vat_Capacity_Ml;   // defined below with the EEPROM settings block

// R-cal: fit used_ml = raw * factor + fixed through the two stored samples.
// Needs them far enough apart, otherwise the slope is noise amplified by a tiny
// denominator - then we keep the single-point meaning (offset stays, slope from
// the newer sample). Returns true when a real two-point fit was applied.
bool resinFitCalibration() {
  bool haveA = calRawA > 0 && calMeasA > 0, haveB = calRawB > 0 && calMeasB > 0;
  // Grams -> ml HERE, with the density in force right now: that is what makes a
  // later density correction re-fit both samples instead of mixing two bases.
  const float mA = calMeasA / resinDensity, mB = calMeasB / resinDensity;
  if (haveA && haveB) {
    // The two rows belong to the user - they may type the bigger print into row 1.
    // Order a LOCAL copy for the maths instead of swapping the stored slots, which
    // would make the rows jump around under whoever is editing them.
    float rLo = calRawA, mLo = mA, rHi = calRawB, mHi = mB;
    if (rLo > rHi) {
      float t = rLo; rLo = rHi; rHi = t;
      t = mLo; mLo = mHi; mHi = t;
    }
    float dr = rHi - rLo;
    if (dr >= 0.5f && dr >= 0.25f * rHi) {              // clearly different sizes
      float k = (mHi - mLo) / dr;
      float f = mLo - k * rLo;
      if (f < 0) f = 0;                                  // negative film is nonsense
      // An offset near the vat size would trip the low-resin stop before layer 1.
      float fMax = RESIN_FIXED_MAX;
      if (Vat_Capacity_Ml > 0 && Vat_Capacity_Ml / 4.0f < fMax) fMax = Vat_Capacity_Ml / 4.0f;
      if (k >= RESIN_CAL_MIN && k <= RESIN_CAL_MAX && f <= fMax) {
        resinCalFactor = k;
        resinFixedMl = f;
        return true;
      }
    }
  }
  // Single usable sample (or the pair was unusable): solve the slope alone and
  // leave the offset as it is - the user can add a second, different-sized print.
  float r = calNewRaw > 0 ? calNewRaw  : (haveB ? calRawB : calRawA);    // newest wins
  float m = (calNewRaw > 0 ? calNewMeas : (haveB ? calMeasB : calMeasA)) / resinDensity;
  if (r > 0 && m > 0) {
    float k = (m - resinFixedMl) / r;
    if (k >= RESIN_CAL_MIN && k <= RESIN_CAL_MAX) resinCalFactor = k;
  }
  return false;
}

// Store one weighed print. Of the three possible pairs (old A+B, A+new, new+B)
// keep the one whose raw values are FURTHEST apart - separation is what makes a
// two-point fit possible at all. (Refreshing "the nearest slot" instead would let
// a run of medium-sized prints quietly collapse the pair back to one point.)
void resinAddSample(float rawMl, float measMl) {
  if (!(rawMl > 0 && measMl > 0)) return;
  calNewRaw = rawMl; calNewMeas = measMl;      // newest, for the 1-sample fallback
  bool haveA = calRawA > 0 && calMeasA > 0, haveB = calRawB > 0 && calMeasB > 0;
  if (!haveA)      { calRawA = rawMl; calMeasA = measMl; }
  // Same-size re-measure must REPLACE slot A, not fill B with a twin: the
  // dashboard retries a slow POST once, and a double click does the same, so
  // an identical sample would otherwise occupy both slots (seen 2026-08-09).
  else if (fabsf(rawMl - calRawA) <= 0.10f * calRawA && !haveB)
                   { calRawA = rawMl; calMeasA = measMl; }
  else if (!haveB) { calRawB = rawMl; calMeasB = measMl; }
  else if (fabsf(rawMl - calRawA) <= 0.10f * calRawA) { calRawA = rawMl; calMeasA = measMl; }
  else if (fabsf(rawMl - calRawB) <= 0.10f * calRawB) { calRawB = rawMl; calMeasB = measMl; }
  else {
    // Neither slot is being re-measured, so keep whichever pair is WIDEST. A
    // middling print that would narrow the pair is ignored on purpose: the spread
    // is what makes a two-point fit possible, and a run of medium-sized prints
    // must not quietly collapse it back to one point (verified by simulation).
    float sAB = calRawB - calRawA;             // A < B is maintained at the end
    float sAn = fabsf(rawMl - calRawA);
    float sNb = fabsf(calRawB - rawMl);
    if (sAn > sAB && sAn >= sNb)  { calRawB = rawMl; calMeasB = measMl; }   // beyond B
    else if (sNb > sAB)           { calRawA = rawMl; calMeasA = measMl; }   // below A
    // else: inside the existing span - dropped, the pair stays as wide as it was
  }
  // Order the pair only once BOTH slots hold a real sample: with an empty slot
  // (-1) the comparison would swap the first sample into the empty one.
  if (calRawA > 0 && calRawB > 0 && calRawA > calRawB) {
    float tr = calRawA, tm = calMeasA;
    calRawA = calRawB; calMeasA = calMeasB; calRawB = tr; calMeasB = tm;
  }
}

// Write ONE slot directly (1 = A, 2 = B). Unlike resinAddSample() this decides
// nothing: the row the user typed into is the row that changes. grams <= 0 clears
// the slot. Returns false only for a bad slot number.
bool resinSetSample(int slot, float rawMl, float grams) {
  float *r = (slot == 1) ? &calRawA  : (slot == 2) ? &calRawB  : nullptr;
  float *m = (slot == 1) ? &calMeasA : (slot == 2) ? &calMeasB : nullptr;
  if (!r) return false;
  if (!(rawMl > 0 && grams > 0)) {
    *r = *m = -1;
    // calNew* may have pointed AT this sample; leaving it would let a deleted
    // measurement keep driving the single-point fallback below.
    calNewRaw = calNewMeas = -1;
  } else {
    *r = rawMl; *m = grams;
    calNewRaw = rawMl; calNewMeas = grams;   // newest, for the 1-sample fallback
  }
  resinFitCalibration();
  // Nothing left to fit from: the fallback would silently keep the old factor.
  if (!(calRawA > 0) && !(calRawB > 0)) { resinCalFactor = 1.0f; resinFixedMl = 0.0f; }
  return true;
}

// Density changed -> both samples mean different ml now. Re-fit at once, so the
// correction always matches the density currently shown.
void resinRefitAfterDensityChange() {
  if (calRawA > 0 || calRawB > 0) resinFitCalibration();
}

void resinClearCalibration() {
  resinCalFactor = 1.0f;
  resinFixedMl = 0.0f;
  calRawA = calMeasA = calRawB = calMeasB = -1;
  calNewRaw = calNewMeas = -1;
}

// ---- Reset-reason telemetry (0-30) -----------------------------------------
// Field case: a print died after the base layers and the printer was found
// rebooted with no model loaded - looked like a power loss, but mains never
// went out. A "prActive" flag is set in NVS at print start (layer checkpointed
// every 25, same cadence as the VAT estimate) and cleared at the single print
// exit. If the flag is still set at boot, the firmware died mid-print: the
// reset reason + last layer are persisted so the dashboard can say "brownout
// during print at ~layer 42" instead of leaving the user guessing.
esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;
bool crashSeen = false;      // a mid-print death record exists (any boot)
uint8_t crashReason = 0;     // its esp_reset_reason value
uint16_t crashLayer = 0;     // last checkpointed layer of that print
uint32_t crashEpoch = 0;     // ~when it died (last checkpoint's NTP epoch; 0 = unknown)

const char *resetReasonName(uint8_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "crash (panic)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brownout (power dip)";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_SDIO:      return "SDIO reset";
    default:                return "unknown";
  }
}

// Wall-clock stamp for the crash record ("when did it die") - 0 while NTP
// has never synced, and the dashboard shows no time then.
uint32_t telemetryEpochNow() {
  time_t nowT = time(nullptr);
  return (uint32_t)(nowT > 1700000000 ? nowT : 0);
}

void savePrintActiveFlag(bool active) {
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putBool("prActive", active);
  if (active) {
    sysPrefs.putUShort("prLayer", 0);
    sysPrefs.putULong("prEpoch", telemetryEpochNow());
  }
  sysPrefs.end();
}

void readBootTelemetry() {  // called once in setup(), after loadDeviceConfig()
  bootResetReason = esp_reset_reason();
  sysPrefs.begin("tinymaker", false);
  if (sysPrefs.getBool("prActive", false)) {
    crashReason = (uint8_t)bootResetReason;
    crashLayer = sysPrefs.getUShort("prLayer", 0);
    crashEpoch = sysPrefs.getULong("prEpoch", 0);
    sysPrefs.putBool("prActive", false);
    sysPrefs.putBool("crashSeen", true);
    sysPrefs.putUChar("crashRsn", crashReason);
    sysPrefs.putUShort("crashLyr", crashLayer);
    sysPrefs.putULong("crashEpo", crashEpoch);
    crashSeen = true;
  } else {  // no fresh death - keep showing the last recorded one
    crashSeen = sysPrefs.getBool("crashSeen", false);
    crashReason = sysPrefs.getUChar("crashRsn", 0);
    crashLayer = sysPrefs.getUShort("crashLyr", 0);
    crashEpoch = sysPrefs.getULong("crashEpo", 0);
  }
  sysPrefs.end();
  DBG("Boot reset reason: %s%s\n", resetReasonName((uint8_t)bootResetReason),
      crashSeen ? " (mid-print death on record)" : "");
}

#if ENABLE_NETWORK
// Self-update (defined in Network.ino) - forward-declared so screen421()
// in Interface.ino and loop() can call them across the #if boundary
// (auto-prototypes are not generated for functions inside #if blocks).
void otaCheckLatest();
void otaCheckLatest(uint16_t timeoutMs);
const char *otaLatestVerStr();
int otaVersionState();
bool otaHasUpdate();
void otaInstallLatest();
void network_service_window(uint16_t durationMs);
void screen422();   // "install from file" screen (Interface.ino, #if-guarded)
void tgNotifyFinished();   // Telegram hooks (TinyMakerTelegram.ino, #if-guarded)
void tgNotifyLowResin();
void tgNotifyCanceled();
void tgNotifyPowerRestored();   // 0.17: power-loss interrupted a print
void tgNotifyLowResinSoon(float ml, int minsToStop);   // 0.17 #40: pre-warn before low-resin stop
void screenBootUpdatePrompt();
void screenBootUpdateDisablePrompt();
#endif

// ===================================================================================
// Pin Definitions
// ===================================================================================

// Button Pins
// Used for UI navigation and control
const int buttonBack = 33; // Back button
const int buttonUp = 32;   // Up button
const int buttonDown = 35; // Down button
const int buttonOK = 34;   // OK/Select button

// Sensor & Actuator Pins
const int end_stop = 26; // Z-axis endstop switch (limit switch)
const int mot_en = 13;   // Motor Enable pin
const int mot_step = 12; // Motor Step pin
const int mot_dir = 14;  // Motor Direction pin
const int LED = 21;      // UV LED control pin
const int FAN = 16;      // Cooling fan control pin
const int SDCS = 25;     // SD Card Chip Select pin

// ULN2003 Motor Driver Pins
// These pins drive the stepper motor coils via ULN2003
#define IN1 12
#define IN2 13
#define IN3 14
#define IN4 22

// initialize the stepper library
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);

// ===================================================================================
// Display Configuration
// ===================================================================================

// Display 1: Masking LCD (UV masking)
// This screen displays the layer image
Arduino_DataBus *bus = new Arduino_ESP32SPI(27 /* DC */, 5 /* CS */, 18 /* SCK */, 23 /* MOSI */, 19 /* MISO */, VSPI /* spi_num */);
Arduino_GFX *gfx1 = new Arduino_ST7789(bus, -1 /* RST */, 1 /* rotation */, true /* IPS */);

// Display 2: UI LCD (User Interface)
// This screen shows the menu and status to the user
Arduino_DataBus *bus2 = new Arduino_ESP32SPI(27 /* DC */, 4 /* CS */, 18 /* SCK */, 23 /* MOSI */, 19 /* MISO */, VSPI /* spi_num */);
Arduino_GFX *gfx2 = new Arduino_ST7735(bus2, -1 /* RST */, 3 /* rotation */, true /* IPS */,80 /* width */, 160 /* height */, 26 /* col offset 1 */, 1 /* row offset 1 */, 26 /* col offset 2 */, 1 /* row offset 2 */);

// ===================================================================================
// Global Variables
// ===================================================================================

// Timing Calculation Variables
// Used to track print time and button press duration
int startTime;
int Duration;
int startTime2;
int Duration2;

// State Variables
int screen = 1;             // Current screen ID
int counter = 0;            // General purpose counter (e.g., used for folder navigation)
long Position_before_pause; // Z-axis position stored when pausing

// Estimation Variables
// Used to calculate object height ang remaining print time
float total_height; // Total height of the object to print
long estimated_seconds;
byte estimated_hours;
byte estimated_minutes;
float motor_updown_time;       // Time taken for one up and down cycle
float motor_updown_time_total; // Total time spent on motor movements

// P-live shared state (0.17): the live-3D silhouette stack. Defined here (the
// first-concatenated file) so Network.ino can serve it from /api/live/slices and
// PNG.ino - both concatenated after this one - can fill it. See PNG.ino for the
// capture logic and the LIVE_* geometry.
/* 80x60, NE 64x48 (V 08-14): tai tas pats tinklelis, kuri turi narsykles kesas, tad
   pries-uzpildymas is slices.tmv tampa paprastu kopijavimu, o telefonas mato lygiai ta
   pati, ka ir kompiuteris. Prie 64x48 sumazinimas uzpildydavo ~19 % daugiau ploto -
   tarpai tarp detales ir jos atramu suaugdavo (ismatuota 08-14 is tikru pjuviu).
   Kaina: buferis 13.8 -> 21.6 KB (imamas spaudinio pradzioje) ir HTTP siuntinys
   19 -> 29 KB (atiduodamas tik po homing'o, kai motoras stovi). */
#define LIVE_GW 80
#define LIVE_GH 60
#define LIVE_MAX_SLICES 36
#define LIVE_SLICE_BYTES ((LIVE_GW * LIVE_GH + 7) / 8)   // 600 bytes, 1 bit/px
uint8_t *liveBuf = NULL;   // LIVE_SLICE_BYTES * liveN, calloc'd per print (NULL = off)
int liveN = 0;             // sampled slices for this print (<= 36)
int liveCaptured = 0;      // slots filled so far (grows as the print proceeds)
// Slots ABOVE liveCaptured hold the model's own silhouettes, pre-loaded from the
// cached slice file at print start (SD is still free there). That is what lets a
// browser opened mid-print draw the un-printed part as a ghost: the live capture
// alone only ever knows layers already exposed. No extra RAM - same buffer.
bool livePrefilled = false;
// Pilnas stekas atiduodamas tik ISEJUS is homing'o: 36 pjuviai = ~29 KB chunked, o
// homing'o cikle HTTP aptarnaujamas tarp zingsniu - toks siuntinys silpname WiFi
// blokuoja `client.write` 1-3 s ir tiek laiko nekvieciamas `stepper.run()` (auditas
// 08-14). Iki tol endpoint'as elgiasi kaip anksciau: atiduoda tik uzfiksuotus.
bool liveReady = false;

// UI Navigation Variables
int setting_item;              // Current selected item in settings menu
bool setting_item_updown = 1;  // Direction indicator for settings (1=up, 0=down)
int advanced_item = 1;         // Current selected item in System -> Advanced
int advanced_group = 1;        // 0-17a: selected Advanced group (1 Network, 2 Resin, 3 Display)
int system_item = 1;           // 0-17b: System menu selection (one screen code 41 + index)
bool printing_item_updown = 1; //1=up,0=down.

// Printing Flags
bool homing_canceled = false; // Flag: Homing process canceled
bool print_paused = false;    // Flag: Print is currently paused
bool print_canceled = false;  // Flag: Print process canceled

// Motion Parameters
long Vat_Capacity_Ml = 15;  // resin vat size to the MAX mark (ml), EEPROM addr 11
float steps_mm = 1463;     // Steps per millimeter for Z-axis
int homing_Feedrate = 300; // Feedrate for homing
float max_height = 68;     // Maximum build height (mm)

// Estimated resin left in the VAT. Seeds to a full VAT on first use (or after
// the capacity setting shrinks below the stored remainder).
float vatRemaining() {
  if (vatRemainingMl < 0 || vatRemainingMl > (float)Vat_Capacity_Ml)
    vatRemainingMl = (float)Vat_Capacity_Ml;
  return vatRemainingMl;
}

// "VAT refilled" action (LCD Advanced item / dashboard button / after refill
// pause): bookkeeping restarts from a full VAT.
void vatMarkRefilled() {
  vatRemainingMl = (float)Vat_Capacity_Ml;
  lowResinNotified = false;
  lowResinPreWarned = false;   // 0.17 #40: re-arm the pre-warn after a refill
  saveVatRemaining();
}

// 0.17 0-16: set the remaining resin from a weighing instead of the mark. The
// mark answers "full or not"; the scale answers "how much", which is what the
// low-resin logic and the per-model estimate actually work with. Needs the empty
// vat's weight (a machine property) and the resin's density (from the profile).
// Returns false when either is missing or the number makes no sense.
bool vatSetFromWeight(float grams) {
  if (vatEmptyG <= 0 || resinDensity <= 0) return false;
  float ml = (grams - vatEmptyG) / resinDensity;
  if (ml < 0) ml = 0;
  if (ml > (float)Vat_Capacity_Ml) ml = (float)Vat_Capacity_Ml;
  vatRemainingMl = ml;
  lowResinNotified = false;
  lowResinPreWarned = false;   // a re-measure re-arms the warning, like a refill
  saveVatRemaining();
  return true;
}

// System State
int current_layer = 0; // Current layer being printed
int current_state = 0; // Current printing state
                       // (0=Homing, 1=Curing, 2=Lifting, 3=Dropping, 4=Canceling)
                       // (5=Pausing, 6=Paused, 7=Resuming, 8=Finish, 10=Refill VAT pause)

// Print Parameters (Loaded from EEPROM) 
float Layer_Height ;        // Layer thickness (mm)
long Base_Exposure ;        // Exposure time for base layers (whole seconds)
long Regular_Exposure ;     // Exposure for normal layers, in DECISECONDS (0.17 0-3; e.g. 140 = 14.0 s)
byte Base_Layer ;           // Number of base layers
byte Transition_Layer ;     // Number of transition layers
byte Slow_Lift_Distance ;   // Distance for slow lift (mm)
byte Fast_Lift_Distance ;   // Distance for fast lift (mm)
int Slow_Lift_Feedrate ;    // Speed for slow lift (mm/min)
int Fast_Lift_Feedrate ;    // Speed for fast lift (mm/min)
int Drop_Back_Feedrate ;    // Speed for retract (mm/min)

// Default Manual Exposure Time
int manual_exposure = 35;

// Exposure calculation helper for transition layers
float Transition_Exposure ; 

// SD card instance
SdFat SD;

// File System Variables
char foldersel_long[101]; // Buffer for long folder names
String foldersel;         // Selected folder name (display version)
int layer_counter;        // Total number of layers
File root;                // Root directory object
String DirAndFile;        // Full path helper
String FileName;          // Current file name

// PNG Decoding
File myfile;
PNG png; // PNG decoder instance

// 0.17 0-3: EEPROM schema version lives in addr 0 (previously unused). v2 stores
// Regular exposure as 2-byte DECISECONDS at addr 12-13; addr 3 still holds the
// rounded whole-second value so a downgrade to <= 0.16 reads a sane number.
#define SETTINGS_SCHEMA_VER 2
#define EE_ADDR_SCHEMA 0
#define EE_ADDR_REG_DS 12

static uint16_t eepromReadU16(int addr) {
  return (uint16_t)EEPROM.read(addr) | ((uint16_t)EEPROM.read(addr + 1) << 8);
}
static void eepromWriteU16(int addr, uint16_t v) {
  EEPROM.write(addr, (uint8_t)(v & 0xFF));
  EEPROM.write(addr + 1, (uint8_t)(v >> 8));
}

void savePrintSettings() {
  // Per-modelio sluoksniai ir laikai skaiciuojami is Layer_Height, tad bet kuris
  // sio bloko irasymas gali padaryti atidarytu pultu sarasus pasenusius - nesvarbu,
  // ar spausta pulte, ar prie printerio (auditas 08-16).
  sdRev++;
  EEPROM.write(EE_ADDR_SCHEMA, SETTINGS_SCHEMA_VER);
  EEPROM.write(1, Layer_Height * 100);
  EEPROM.write(2, Base_Exposure);
  EEPROM.write(3, (uint8_t)lroundf(Regular_Exposure / 10.0f));   // downgrade-safe whole seconds
  eepromWriteU16(EE_ADDR_REG_DS, (uint16_t)Regular_Exposure);    // canonical: deciseconds
  EEPROM.write(4, Base_Layer);
  EEPROM.write(5, Transition_Layer);
  EEPROM.write(6, Slow_Lift_Distance);
  EEPROM.write(7, Fast_Lift_Distance);
  EEPROM.write(8, Slow_Lift_Feedrate);
  EEPROM.write(9, Fast_Lift_Feedrate);
  EEPROM.write(10, Drop_Back_Feedrate);
  EEPROM.write(11, Vat_Capacity_Ml);
  EEPROM.commit();
}

// Safety net for a bad calibration: whenever Regular exposure is REPLACED
// (exposure-test pick or a dashboard config save - not the +-1 LCD steps),
// the old value is remembered so the dashboard can offer a one-click Undo.
// Undo goes through the same path, so undo-of-undo swaps back.
void rememberPrevRegularExposure(long oldVal) {
  if (oldVal <= 0 || oldVal > 300 || oldVal == Regular_Exposure) return;   // deciseconds now
  prevRegularExposure = (uint16_t)oldVal;
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putUShort("prevRegDs", prevRegularExposure);
  sysPrefs.end();
}

// Tas pats Base'ui: derinant butent ji dazniausiai persukama, o be atsarginio
// kelio tenka atsiminti sena reiksme galvoje (V 08-12).
void rememberPrevBaseExposure(long oldVal) {
  if (oldVal < 5 || oldVal > 60 || oldVal == Base_Exposure) return;   // sveikos sekundes
  prevBaseExposure = (uint8_t)oldVal;
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putUChar("prevBaseS", prevBaseExposure);
  sysPrefs.end();
}

// ===================================================================================
// Settings backup & restore (flat JSON, on SD or via the dashboard)
// ===================================================================================
// One file holds every user setting AND the lifetime counters, so a full USB
// reflash (which wipes both EEPROM and NVS) can be undone from the SD card.
// Compiled unconditionally - the boot-time SD restore must work network-free.
static const char *BACKUP_PATH = "/tinymaker-backup.json";
bool settingsWereFactoryReset = false;  // set when setup() seeds factory defaults

String backupEscape(const String &v) {
  String out;
  for (size_t i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c != '\n' && c != '\r') out += c;
  }
  return out;
}

String buildConfigBackupJson(bool includeSecrets = true) {
  String out = "{\"backupVersion\":1,\"firmware\":\"";
  out.reserve(2560);   // ~127 appends; one caller runs with TLS allocated
#ifdef FIRMWARE_VERSION
  out += FIRMWARE_VERSION;
#endif
  // Kept near the front so a light 512-byte read (sdBackupSavedEpoch) sees it.
  // 0 when the clock is not NTP-synced (epoch below ~2023 = not real time).
  out += "\",\"savedAtEpoch\":";
  {
    time_t nowT = time(nullptr);
    out += String((uint32_t)(nowT > 1700000000 ? nowT : 0));
  }
  out += ",\"layerHeight\":";
  out += String(Layer_Height, 2);
  out += ",\"baseExposure\":";
  out += String(Base_Exposure);
  out += ",\"regularExposure\":";
  out += String(Regular_Exposure / 10.0, 1);   // 0.17 0-3: seconds (ds/10) - human-readable + downgrade-safe
  out += ",\"baseLayers\":";
  out += String(Base_Layer);
  out += ",\"transitionLayers\":";
  out += String(Transition_Layer);
  out += ",\"slowLiftDistance\":";
  out += String(Slow_Lift_Distance);
  out += ",\"fastLiftDistance\":";
  out += String(Fast_Lift_Distance);
  out += ",\"slowLiftFeedrate\":";
  out += String(Slow_Lift_Feedrate);
  out += ",\"fastLiftFeedrate\":";
  out += String(Fast_Lift_Feedrate);
  out += ",\"dropBackFeedrate\":";
  out += String(Drop_Back_Feedrate);
  out += ",\"vatMl\":";
  out += String(Vat_Capacity_Ml);
  out += ",\"lowResinPause\":";
  out += lowResinPauseEnabled ? "true" : "false";
  out += ",\"lowResinMl\":";
  out += String(lowResinThresholdMl);
  out += ",\"lowResinWarnMl\":";
  out += String(lowResinWarnMl);
  out += ",\"resinCalFactor\":";
  out += String(resinCalFactor, 3);   // R-cal: 3 decimals - a rounded 1 would undo it
  out += ",\"resinFixedMl\":";
  out += String(resinFixedMl, 2);
  out += ",\"resinDensity\":";
  out += String(resinDensity, 3);
  out += ",\"resinProfile\":\"";   // 0.17 0-16: the slug; the file itself lives on the card
  out += backupEscape(resinProfileName);
  out += "\",\"vatEmptyG\":";
  out += String(vatEmptyG, 2);
  out += ",\"calRawA\":";   out += String(calRawA, 2);
  out += ",\"calMeasA\":";  out += String(calMeasA, 2);
  out += ",\"calRawB\":";   out += String(calRawB, 2);
  out += ",\"calMeasB\":";  out += String(calMeasB, 2);
  out += ",\"calUnit\":1";   // calMeas* = GRAMAI; be sios zymos senas
                            // backup as (mililitrai) atkurtu klaidinga kalibracija
  out += ",\"askRefill\":";
  out += askRefillEnabled ? "true" : "false";
  out += ",\"slicerOn\":";           /* 0.17 SL-mod: be sito po pilno reflash'o
                                       jungiklis tyliai grizta i OFF, o su juo
                                       dingsta ir slicerio kortele (auditas 08-22) */
  out += slicerModuleOn ? "true" : "false";
  out += ",\"previewFlip\":";
  out += previewFlip ? "true" : "false";
  out += ",\"uiTimeout\":";
  out += String(uiTimeoutSecs);
  out += ",\"dryRun\":";
  out += uvLedEnabled ? "false" : "true";
  out += ",\"wifiEnabled\":";
  out += wifiEnabled ? "true" : "false";
  out += ",\"webDashboardEnabled\":";
  out += webDashboardEnabled ? "true" : "false";
  out += ",\"bootUpdateCheck\":";
  out += bootUpdateCheckEnabled ? "true" : "false";
  out += ",\"resumeEnabled\":";
  out += resumeEnabled ? "true" : "false";
  out += ",\"resumePrecise\":";
  out += resumePrecise ? "true" : "false";
  out += ",\"pauseLiftMm\":";
  out += String(pauseLiftMm);
  out += ",\"bootAnim\":\"";
  out += backupEscape(bootAnimName);
  out += "\"";
  out += ",\"mqttEnabled\":";
  out += mqttEnabled ? "true" : "false";
  out += ",\"mqttHost\":\"";
  out += backupEscape(mqttHost);
  out += "\",\"mqttPort\":";
  out += String(mqttPort);
  out += ",\"mqttUser\":\"";
  out += backupEscape(mqttUser);
  if (includeSecrets) {
    out += "\",\"mqttPass\":\"";
    out += backupEscape(mqttPass);
  }
  out += "\",\"mqttTopic\":\"";
  out += backupEscape(mqttTopic);
  out += "\",\"tgEnabled\":";
  out += tgEnabled ? "true" : "false";
  if (includeSecrets) {
    out += ",\"tgToken\":\"";
    out += backupEscape(tgToken);
    out += "\"";
  }
  out += ",\"tgChat\":\"";
  out += backupEscape(tgChat);
  out += "\",\"waEnabled\":";
  out += waEnabled ? "true" : "false";
  out += ",\"waPhone\":\"";
  out += backupEscape(waPhone);
  out += "\",\"waApiKey\":\"";
  out += backupEscape(waApiKey);
  out += "\",\"dcEnabled\":";
  out += dcEnabled ? "true" : "false";
  out += ",\"dcWebhook\":\"";
  out += backupEscape(dcWebhook);
  out += "\",\"connectEnabled\":";
  out += connectEnabled ? "true" : "false";
  out += ",\"connectBaseUrl\":\"";
  out += backupEscape(connectBaseUrl);
  out += "\",\"connectPrinterName\":\"";
  out += backupEscape(connectPrinterName);
  out += "\",\"connectLeaderboard\":";
  out += connectLeaderboardOptIn ? "true" : "false";
  out += ",\"connectPublicId\":\"";
  out += backupEscape(connectPrinterPublicId);
  out += "\"";
  if (includeSecrets) {
    out += ",\"connectToken\":\"";
    out += backupEscape(connectPublishToken);
    out += "\",\"connectRecoveryCode\":\"";
    out += backupEscape(connectRecoveryCode);
    out += "\"";
  }
  out += ",\"connectAutoBackup\":";
  out += connectAutoBackup ? "true" : "false";
  out += ",\"connectBackupEpoch\":";
  out += String(connectBackupEpoch);
  out += ",\"statsPing\":";
  out += statsPingEnabled ? "true" : "false";
  out += ",\"printSecs\":";
  out += String(totalPrintSecs);
  out += ",\"uvLedSecs\":";
  out += String(totalUvLedSecs);
  out += ",\"vatRemainingMl\":";
  out += String(vatRemainingMl, 1);
  out += "}";
  return out;
}

// --- tiny extractors for OUR OWN flat backup format (not a general parser) ---
static int backupFind(const String &j, const char *key) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  int p = j.indexOf(needle);
  if (p < 0) return -1;
  p += needle.length();
  // Hand-edited / pretty-printed backups put a space after ':'. The reader used
  // to land ON that space and quietly read every boolean as false - one such
  // restore switched the printer's WiFi off (08-10). Numbers only survived
  // because atof() skips whitespace by itself.
  while (p < (int)j.length() &&
         (j[p] == ' ' || j[p] == '\t' || j[p] == '\r' || j[p] == '\n')) p++;
  return p;
}

double backupNum(const String &j, const char *key, double def) {
  int p = backupFind(j, key);
  return p < 0 ? def : atof(j.c_str() + p);   // atof stops at ',' or '}'
}

bool backupBool(const String &j, const char *key, bool def) {
  int p = backupFind(j, key);
  return p < 0 ? def : j.startsWith("true", p);
}

String backupStr(const String &j, const char *key, const String &def) {
  int p = backupFind(j, key);
  if (p < 0 || p >= (int)j.length() || j[p] != '"') return def;
  String out;
  bool esc = false;
  for (int i = p + 1; i < (int)j.length(); i++) {
    char c = j[i];
    if (esc) { out += c; esc = false; }
    else if (c == '\\') esc = true;
    else if (c == '"') break;
    else out += c;
  }
  return out;
}

static long backupClamp(double v, long lo, long hi) {
  long n = (long)v;
  if (n < lo) return lo;
  if (n > hi) return hi;
  return n;
}

// Apply a backup: same value clamps as the web config form (applyConfigRequest),
// so a hand-edited or stale file can't smuggle absurd values in.
void applyConfigBackup(const String &j) {
  // A restore swaps the whole recipe, resin name included: the last print was
  // made with the resin being replaced, so a weighing against it would
  // calibrate a stranger's print (same trap as a profile switch, audit 08-16).
  lastPrintRawMl = -1;   // persisted with the rest, at the end of this function
  Layer_Height = backupNum(j, "layerHeight", Layer_Height) < 0.075 ? 0.05 : 0.10;
  Base_Exposure = backupClamp(backupNum(j, "baseExposure", Base_Exposure), 5, 60);   // 0.17 0-3: base min 5 s
  // 0.17 0-3: backup stores Regular in SECONDS (downgrade-readable); convert to
  // deciseconds on restore, preserving 0.1 s (backupClamp->long would drop it).
  {
    double regSecs = backupNum(j, "regularExposure", Regular_Exposure / 10.0);
    long regDs = lroundf((float)regSecs * 10.0f);
    if (regDs < 10) regDs = 10; else if (regDs > 300) regDs = 300;
    Regular_Exposure = regDs;
  }
  Base_Layer = backupClamp(backupNum(j, "baseLayers", Base_Layer), 1, 8);
  Transition_Layer = backupClamp(backupNum(j, "transitionLayers", Transition_Layer), 0, 10);
  Slow_Lift_Distance = backupClamp(backupNum(j, "slowLiftDistance", Slow_Lift_Distance), 1, 3);
  Fast_Lift_Distance = backupClamp(backupNum(j, "fastLiftDistance", Fast_Lift_Distance), 1, 3);
  Slow_Lift_Feedrate = backupClamp(backupNum(j, "slowLiftFeedrate", Slow_Lift_Feedrate), 20, 50);
  Fast_Lift_Feedrate = backupClamp(backupNum(j, "fastLiftFeedrate", Fast_Lift_Feedrate), 20, 50);
  Drop_Back_Feedrate = backupClamp(backupNum(j, "dropBackFeedrate", Drop_Back_Feedrate), 20, 50);
  Vat_Capacity_Ml = backupClamp(backupNum(j, "vatMl", Vat_Capacity_Ml), 10, 40);
  lowResinPauseEnabled = backupBool(j, "lowResinPause", lowResinPauseEnabled);
  lowResinThresholdMl = backupClamp(backupNum(j, "lowResinMl", lowResinThresholdMl), 1, 3);
  lowResinWarnMl = backupClamp(backupNum(j, "lowResinWarnMl", lowResinWarnMl), 3, 15);
  // R-cal: fractional - backupClamp() casts to long and would turn 1.35 into 1.
  {
    float cal = (float)backupNum(j, "resinCalFactor", resinCalFactor);
    if (cal >= RESIN_CAL_MIN && cal <= RESIN_CAL_MAX) resinCalFactor = cal;
    float fx = (float)backupNum(j, "resinFixedMl", resinFixedMl);
    if (fx >= 0.0f && fx <= RESIN_FIXED_MAX) resinFixedMl = fx;
    float dn = (float)backupNum(j, "resinDensity", resinDensity);
    if (dn >= 0.8f && dn <= 2.0f) resinDensity = dn;
    // 0.17 0-16: only the name comes back - the profile file lives on the card,
    // so a restore onto a different card simply leaves the values as restored.
    resinProfileName = sanitizeSlug(backupStr(j, "resinProfile", resinProfileName), "");
    float ve = (float)backupNum(j, "vatEmptyG", vatEmptyG);
    if (ve > 0.0f && ve <= VAT_EMPTY_G_MAX) vatEmptyG = ve;
    // Samples travel with the factor - otherwise a restored printer reports
    // "not calibrated" and the next weighing starts the pair over.
    calRawA  = (float)backupNum(j, "calRawA", calRawA);
    calMeasA = (float)backupNum(j, "calMeasA", calMeasA);
    calRawB  = (float)backupNum(j, "calRawB", calRawB);
    calMeasB = (float)backupNum(j, "calMeasB", calMeasB);
    if (!(calRawA > 0 && calMeasA > 0)) { calRawA = calMeasA = -1; }
    if (!(calRawB > 0 && calMeasB > 0)) { calRawB = calMeasB = -1; }
    // Backups written before samples moved to grams carry ml and no marker -
    // convert. But ONLY when the backup actually carried samples: a 0.16.x file
    // has no cal keys at all, so calMeas* above kept the PRINTER'S current grams
    // - converting those would silently multiply a good calibration by the
    // density on every old-backup restore (found in review 08-10, before ship).
    bool hadCal = backupFind(j, "calMeasA") >= 0 || backupFind(j, "calMeasB") >= 0;
    if (hadCal && (int)backupNum(j, "calUnit", 0) != 1) {
      if (calMeasA > 0) calMeasA *= resinDensity;
      if (calMeasB > 0) calMeasB *= resinDensity;
    }
  }
  askRefillEnabled = backupBool(j, "askRefill", askRefillEnabled);
  slicerModuleOn = backupBool(j, "slicerOn", slicerModuleOn);   // 0.17 SL-mod
  previewFlip = backupBool(j, "previewFlip", previewFlip);
  uiTimeoutSecs = backupClamp(backupNum(j, "uiTimeout", uiTimeoutSecs), 0, 3600);
  uvLedEnabled = !backupBool(j, "dryRun", !uvLedEnabled);
  wifiEnabled = backupBool(j, "wifiEnabled", wifiEnabled);
  webDashboardEnabled = wifiEnabled && backupBool(j, "webDashboardEnabled", webDashboardEnabled);
  bootUpdateCheckEnabled = backupBool(j, "bootUpdateCheck", bootUpdateCheckEnabled);
  resumeEnabled = backupBool(j, "resumeEnabled", resumeEnabled);
  resumePrecise = backupBool(j, "resumePrecise", resumePrecise);
  pauseLiftMm = backupClamp(backupNum(j, "pauseLiftMm", pauseLiftMm), 20, 40);
  bootAnimName = backupStr(j, "bootAnim", bootAnimName);
  mqttEnabled = wifiEnabled && backupBool(j, "mqttEnabled", mqttEnabled);
  mqttHost = backupStr(j, "mqttHost", mqttHost);
  mqttPort = backupClamp(backupNum(j, "mqttPort", mqttPort), 1, 65535);
  mqttUser = backupStr(j, "mqttUser", mqttUser);
  // Secret fields are optional in Connect backups. If omitted, keep the
  // locally stored value instead of blanking the credential on restore.
  mqttPass = backupStr(j, "mqttPass", mqttPass);
  mqttTopic = backupStr(j, "mqttTopic", mqttTopic);
  if (mqttTopic.length() == 0) mqttTopic = "TinyMaker";
  tgEnabled = wifiEnabled && backupBool(j, "tgEnabled", tgEnabled);
  tgToken = backupStr(j, "tgToken", tgToken);
  tgChat = backupStr(j, "tgChat", tgChat);
  waEnabled = wifiEnabled && backupBool(j, "waEnabled", waEnabled);
  waPhone = backupStr(j, "waPhone", waPhone);
  waApiKey = backupStr(j, "waApiKey", waApiKey);
  dcEnabled = wifiEnabled && backupBool(j, "dcEnabled", dcEnabled);
  dcWebhook = backupStr(j, "dcWebhook", dcWebhook);
  if (tgEnabled) { waEnabled = false; dcEnabled = false; }  // one channel at a time
  else if (waEnabled) dcEnabled = false;
  connectEnabled = wifiEnabled && backupBool(j, "connectEnabled", connectEnabled);
  connectBaseUrl = backupStr(j, "connectBaseUrl", connectBaseUrl);
  connectPrinterName = backupStr(j, "connectPrinterName", connectPrinterName);
  if (connectEnabled) {
    connectLeaderboardOptIn = backupBool(j, "connectLeaderboard", connectLeaderboardOptIn);
  }
  connectPrinterPublicId = backupStr(j, "connectPublicId", connectPrinterPublicId);
  connectPublishToken = backupStr(j, "connectToken", connectPublishToken);
  connectRecoveryCode = backupStr(j, "connectRecoveryCode", connectRecoveryCode);
  connectAutoBackup = backupBool(j, "connectAutoBackup", connectAutoBackup);
  connectBackupEpoch = (uint32_t)backupNum(j, "connectBackupEpoch", connectBackupEpoch);
  statsPingEnabled = backupBool(j, "statsPing", statsPingEnabled);
  totalPrintSecs = (uint32_t)backupNum(j, "printSecs", totalPrintSecs);
  totalUvLedSecs = (uint32_t)backupNum(j, "uvLedSecs", totalUvLedSecs);
  vatRemainingMl = (float)backupNum(j, "vatRemainingMl", vatRemainingMl);

  savePrintSettings();
  saveDeviceConfig();
  saveVatRemaining();
  // Atkurus VISA faila „ankstesne ekspozicijos reiksme" nebeturi prasmes: ji
  // rodytu ne pries atkurima buvusia, o kazkokia senesne, atsitiktine. Tad
  // Undo pasiulymus nuimam - jie atsiras vel pakeitus reiksme is pulto.
  prevRegularExposure = 0;
  prevBaseExposure = 0;
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putULong("printSecs", totalPrintSecs);
  sysPrefs.putULong("uvLedSecs", totalUvLedSecs);
  sysPrefs.putUShort("prevRegDs", 0);
  sysPrefs.putUChar("prevBaseS", 0);
  sysPrefs.putFloat("lastPrintMl", lastPrintRawMl);   // see the top of this function
  sysPrefs.end();
  // Per-model times and layer counts are computed from Layer_Height, which this
  // function just replaced: every open list is stale until it refetches.
  sdRev++;
}

bool sdBackupExists() {
  File f = SD.open(BACKUP_PATH);
  if (!f) return false;
  f.close();
  return true;
}

// savedAtEpoch from the SD backup (0 if absent/unknown). savedAtEpoch sits near
// the front of the file, so a 512-byte read is enough. Assumes SD is ready.
uint32_t sdBackupSavedEpoch() {
  File f = SD.open(BACKUP_PATH);
  if (!f) return 0;
  String j;
  j.reserve(512);
  while (f.available() && j.length() < 512) j += (char)f.read();
  f.close();
  return (uint32_t)backupNum(j, "savedAtEpoch", 0);
}

bool writeBackupToSd() {
  SD.remove((char *)BACKUP_PATH);
  File f = SD.open(BACKUP_PATH, FILE_WRITE);
  if (!f) return false;
  f.print(buildConfigBackupJson());
  f.close();
  return true;
}

bool restoreFromSdBackup() {
  File f = SD.open(BACKUP_PATH);
  if (!f) return false;
  String j;
  j.reserve(1024);
  while (f.available() && j.length() < 4096) j += (char)f.read();
  f.close();
  if (backupNum(j, "backupVersion", 0) < 1) return false;
  applyConfigBackup(j);
  return true;
}

// Continue the boot sequence after the SD-restore prompt (screen 426) or a
// discarded resume prompt (screen 427) - the network (and its possible
// boot-update prompt) run only after them.
void finishRestorePromptBoot() {
  // A restore may have just brought back the settings an interrupted print
  // needs (layer height must match) - check for a resume checkpoint here too.
  if (screen != 427 && resumeLoad()) {
    screenResumePrompt();
    return;
  }
  // Past this point no resume is waiting any more: the prompt was answered and
  // the checkpoint is gone. The flag used to stay up until a resumed print
  // actually started, so after a Discard it was never lowered at all - which
  // left anything gated on it (the boot update check, and since 0-16 the resin
  // profile routes) blocked until the next reboot.
  resumeBootPending = false;
  // The user just answered the prompt with a button press. If that finger is
  // still down when network_setup runs, its "hold BACK at power-on = erase
  // WiFi credentials" emergency check mistakes the held Discard press for
  // the reset gesture and WIPES the credentials (field finding 07-22: every
  // Discard sent the printer back to the setup portal). Wait for release.
  while (digitalRead(buttonBack) == LOW || digitalRead(buttonOK) == LOW ||
         digitalRead(buttonUp) == LOW || digitalRead(buttonDown) == LOW) {
    delay(10);
  }
  #if ENABLE_NETWORK
  network_setup();
  if (screen == 424 || screen == 425) return;   // boot update prompt took over
  #endif
  screen1();
}

bool printerBusy() {
  if (sdJobKind.length() > 0) return true;   // a deferred delete/import owns the SD
  return screen == 1111 || screen == 1112 || screen == 11111 ||
         screen == 11112 || screen == 11113;
}

// Wake a blanked (UI-timeout) status screen. Network events call this before
// drawing - web-started updates/uploads must be visible on the printer.
void uiWakeScreen() {
  if (uiBlanked) {
    uiBlanked = false;
    ((Arduino_TFT *)gfx2)->displayOn();
  }
  lastUiActivityMs = millis();
}

#if ENABLE_NETWORK
// 0-22: auto-dim DURING a print, after the same UI timeout. Called from the
// exposure wait loop (the only place buttons are polled while printing).
// Returns true when the caller must skip its own button handlers this pass:
// either the screen is dimmed (presses may only wake it - never pause/stop,
// V's rule: the waking press is swallowed) or it just woke.
bool printSaverTick() {
  if (uiTimeoutSecs == 0) return false;
  // Only the plain print screen dims - not the stop/pause confirm overlays,
  // not pause/cancel states (the user is at the machine then).
  if (!uiDimmedPrint &&
      (screen != 1111 || print_paused || print_canceled ||
       current_state < 1 || current_state > 3)) return false;

  bool pressed = digitalRead(buttonBack) == LOW || digitalRead(buttonUp) == LOW ||
                 digitalRead(buttonDown) == LOW || digitalRead(buttonOK) == LOW;

  if (uiDimmedPrint) {
    if (pressed || uiSaverWakeQueued) {   // queued: a press landed mid-move
      uiSaverWakeQueued = false;
      uiDimmedPrint = false;
      lastUiActivityMs = millis();
      screen1111();            // full bright redraw + state bar
      screen1111_state();
      while (digitalRead(buttonBack) == LOW || digitalRead(buttonUp) == LOW ||
             digitalRead(buttonDown) == LOW || digitalRead(buttonOK) == LOW) {
        delay(10);             // swallow the waking press entirely
      }
    } else if (millis() - uiSaverLastMoveMs >= 2000UL) {
      uiSaverLastMoveMs = millis();
      uiSaverPos = (uiSaverPos + 1) % 5;
      drawPrintSaver(uiSaverPos);   // drift + refresh the live progress
    }
    return true;               // dimmed (or just woke): callers skip buttons
  }

  if (pressed) { lastUiActivityMs = millis(); return false; }
  if (millis() - lastUiActivityMs >= (unsigned long)uiTimeoutSecs * 1000UL) {
    uiDimmedPrint = true;
    uiSaverPos = 0;
    uiSaverLastMoveMs = millis();
    drawPrintSaver(uiSaverPos);
    return true;
  }
  return false;
}
#endif

bool handleUiTimeout() {
  bool buttonPressed = digitalRead(buttonBack) == LOW ||
                       digitalRead(buttonUp) == LOW ||
                       digitalRead(buttonDown) == LOW ||
                       digitalRead(buttonOK) == LOW;
  if (buttonPressed) {
    lastUiActivityMs = millis();
    if (uiBlanked) {
      uiBlanked = false;
      ((Arduino_TFT *)gfx2)->displayOn();
      screen1();
      delay(200);
      return true;                  // consume wake press
    }
  }

  if (uiTimeoutSecs == 0 || printerBusy()) return false;

  // Already idling: drift the dim text between 5 spots (4 corners + centre)
  // every couple of seconds (0-21 screen saver) so nothing burns in.
  if (uiBlanked) {
#if ENABLE_NETWORK
    if (millis() - uiSaverLastMoveMs >= 2000UL) {
      uiSaverLastMoveMs = millis();
      uiSaverPos = (uiSaverPos + 1) % 5;
      drawIdleScreen(uiSaverPos);
    }
#endif
    return false;
  }

  if (!(screen == 1 || screen == 2 || screen == 3 || screen == 4)) return false;
  if (millis() - lastUiActivityMs < (unsigned long)uiTimeoutSecs * 1000UL) return false;

  // The backlight is hard-wired on, so displayOff() would leave a lit black
  // panel. Draw a dim status instead (0-15/0-21): same power, but the printer
  // looks alive and its IP stays visible.
#if ENABLE_NETWORK
  uiSaverPos = 0;
  uiSaverLastMoveMs = millis();
  drawIdleScreen(uiSaverPos);
#else
  gfx2->fillScreen(BLACK);
  ((Arduino_TFT *)gfx2)->displayOff(); // no network build: nothing useful to show
#endif
  uiBlanked = true;
  return false;
}

// ===================================================================================
// Settings
// ===================================================================================
// -------------------------------------------------------------------------------
// Gamyklinis atstatymas yra daugiau nei EEPROM blokas: dervos profilio vardas
// turi sekti skaicius, jo overlay failas - dingti, o kiekvienas atidarytas pultas
// suzinoti, kad jo sarasai pasene. Abu keliai (printerio Settings ir
// /api/config/defaults) eina per SITA funkcija, kad nebeissiskirtu (auditas 08-16).
void resetEverythingToFactory() {
  resinProfileName = "slow";
  // Overlay turi dingti ir tinklo neturinciame build'e: ten dervu meniu irgi yra,
  // ir „slow" grazintu sena redagavima, kai masina jau suka gamyklinius skaicius.
  // Guard'as tik aplink sdCardReady() - jis vienintelis gyvena Network.ino viduje.
  #if ENABLE_NETWORK
  if (sdCardReady())
  #endif
    resinDropBuiltinOverlays();   // ne vien „slow": fast.json irgi (auditas 08-17)
  lastPrintRawMl = -1;   // resinProf irasys saveDeviceConfig(), cia tik sitas
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putFloat("lastPrintMl", lastPrintRawMl);
  sysPrefs.end();
  resinProfileRev++;
  sdRev++;              // sluoksnio aukstis - gamyklinis: visi sarasai pasene
  resetSettingsToDefault();
  resinClearCalibration();
  resinDensity = RESIN_DENSITY_DEF;
  // Tuscio vato svoris irgi yra SVERIMAS, o patvirtinimo ekranas zada, kad
  // sverimai dingsta. Be sito po atstatymo likutis ml butu skaiciuojamas is
  // seno vartotojo svorio, o ekranas sakytu kitaip (auditas 08-17).
  vatEmptyG = VAT_EMPTY_G_DEF;
  // Iranginio nustatymai - cia pat, kad printerio mygtukas atstatytu tiek pat, kiek
  // pulto: klausimas "Reset settings?" zada ir siuos (auditas 08-16).
  uiTimeoutSecs = 60;
  uvLedEnabled = true;
  wifiEnabled = true;
  webDashboardEnabled = true;
  bootUpdateCheckEnabled = true;
  resumeEnabled = true;
  resumePrecise = false;
  pauseLiftMm = 20;
  // „Undo" turi rodyti i tai, kas buvo pakeista, o po atstatymo tokio dalyko nera.
  prevRegularExposure = 0;
  prevBaseExposure = 0;
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putUShort("prevRegDs", 0);
  sysPrefs.putUChar("prevBaseS", 0);
  sysPrefs.end();
  // saveDeviceConfig() - kvieciancio reikalas (LCD ir web kviecia po viena karta);
  // jis irgi irasys resinProf, tad cia to nekartojam.
}

/**
 * @brief Write factory-default print settings to EEPROM and reload them into
 * the live globals. Single source of truth for the NUMBERS; the full factory
 * reset (profile name, overlay, calibration) lives in resetEverythingToFactory()
 * above and calls this. Layer_Height is stored x100 (10 -> 0.10 mm).
 */
void resetSettingsToDefault() {
  EEPROM.write(EE_ADDR_SCHEMA, SETTINGS_SCHEMA_VER);
  EEPROM.write(1, 10);   // Layer_Height     -> 0.10 mm
  EEPROM.write(2, 35);   // Base_Exposure
  EEPROM.write(3, 14);   // Regular_Exposure whole-second mirror (downgrade-safe)
  eepromWriteU16(EE_ADDR_REG_DS, 140);  // Regular_Exposure = 14.0 s in deciseconds
  EEPROM.write(4, 2);    // Base_Layer
  EEPROM.write(5, 5);    // Transition_Layer
  EEPROM.write(6, 1);    // Slow_Lift_Distance
  EEPROM.write(7, 2);    // Fast_Lift_Distance
  EEPROM.write(8, 40);   // Slow_Lift_Feedrate
  EEPROM.write(9, 50);   // Fast_Lift_Feedrate
  EEPROM.write(10, 50);  // Drop_Back_Feedrate
  EEPROM.write(11, 15);  // Vat_Capacity_Ml (ml to MAX mark)
  EEPROM.commit();

  Layer_Height = EEPROM.read(1) / 100.00;
  Base_Exposure = EEPROM.read(2);
  Regular_Exposure = eepromReadU16(EE_ADDR_REG_DS);   // deciseconds
  Base_Layer = EEPROM.read(4);
  Transition_Layer = EEPROM.read(5);
  Slow_Lift_Distance = EEPROM.read(6);
  Fast_Lift_Distance = EEPROM.read(7);
  Slow_Lift_Feedrate = EEPROM.read(8);
  Fast_Lift_Feedrate = EEPROM.read(9);
  Drop_Back_Feedrate = EEPROM.read(10);
  Vat_Capacity_Ml = EEPROM.read(11);
}

// ===================================================================================
// Setup Function
// ===================================================================================
/**
 * @brief Setup Function
 * Initializes all hardware components, loads settings, and sets the initial state
 */
void setup() {

  #if ENABLE_SERIAL_DEBUG
  Serial.begin(115200);
  #endif

  // -----------------------------------------------------------------------------------
  // Pin Configuration
  // -----------------------------------------------------------------------------------
  pinMode(buttonBack, INPUT);
  pinMode(buttonUp, INPUT);
  pinMode(buttonDown, INPUT);
  pinMode(buttonOK, INPUT);
  pinMode(end_stop, INPUT);
  pinMode(LED, OUTPUT);
  pinMode(FAN, OUTPUT);
  digitalWrite(LED, LOW); 
  digitalWrite(FAN, LOW);

  // -----------------------------------------------------------------------------------
  // Stepper Motor Configuration / 步进电机配置
  // -----------------------------------------------------------------------------------
  // Set maximum speed in steps per second
  // Higher values mean faster movement but less torque  
  stepper.setMaxSpeed(1200.0);
  
  // Set acceleration in steps per second^2. Controls how fast the motor ramps up to speed  
  stepper.setAcceleration(2500.0);
  
  // Disable motor outputs initially to save power and reduce heat when idle  
  stepper.disableOutputs();

  // -----------------------------------------------------------------------------------
  // SD Card Initialization / SD 卡初始化
  // -----------------------------------------------------------------------------------
  // Initialize SD card using the dedicated Chip Select pin and a safe SPI frequency (16MHz) 
  if (SD.begin(SDCS, SD_SCK_MHZ(16))) {
    cleanupManagedSdTemps();
  }
    
  // -----------------------------------------------------------------------------------
  // Displays Initialization / 显示屏初始化
  // -----------------------------------------------------------------------------------
  // Display 1: Masking LCD (UV masking)  
  gfx1->begin();
  gfx1->fillScreen(BLACK);  
  
  // Display 2: UI LCD (User Interface)
  gfx2->begin();
  gfx2->fillScreen(BLACK);
  
  // Display Welcome Screen
  screen0();

  // -----------------------------------------------------------------------------------
  // Settings Loading
  // -----------------------------------------------------------------------------------
  // Initialize EEPROM with 24 bytes of space to read stored parameters.  
  EEPROM.begin(24);

  // Read stored values from specific addresses.
  // Layer Height is stored multiplied by 100 to save as integer, so divide by 100.00 to restore float  
  Layer_Height = EEPROM.read(1) / 100.00;
  Base_Exposure = EEPROM.read(2);
  // Regular_Exposure is loaded below (0.17 0-3 migration: deciseconds at addr 12-13).
  Base_Layer = EEPROM.read(4);
  Transition_Layer = EEPROM.read(5);
  Slow_Lift_Distance = EEPROM.read(6);
  Fast_Lift_Distance = EEPROM.read(7);
  Slow_Lift_Feedrate = EEPROM.read(8);
  Fast_Lift_Feedrate = EEPROM.read(9);
  Drop_Back_Feedrate = EEPROM.read(10);

  // First boot after a full flash leaves EEPROM uninitialized (every byte
  // reads 0xFF = 255), which produced Layer_Height 2.55 mm and absurd
  // exposures/print-time estimates. Seed the same factory defaults the
  // Settings -> "Back to Default" menu uses when values are out of range.
  if (EEPROM.read(1) == 255 || Layer_Height < 0.01 || Layer_Height > 0.2) {
    resetSettingsToDefault();
    settingsWereFactoryReset = true;   // a full reflash wiped the settings
  }

  // 0.17 0-3: Regular exposure moved to 2-byte DECISECONDS (addr 12-13). A
  // pre-0.17 install has schema (addr 0) != v2; migrate ONCE by scaling the old
  // whole-second byte (addr 3) x10. resetSettingsToDefault() above already wrote
  // schema=v2 + the ds value on a fresh flash, so that path takes the else here.
  if (EEPROM.read(EE_ADDR_SCHEMA) != SETTINGS_SCHEMA_VER) {
    uint8_t regS = EEPROM.read(3);
    if (regS < 1 || regS > 30) regS = 14;   // sane-guard the old byte before scaling
    Regular_Exposure = (long)regS * 10;
    eepromWriteU16(EE_ADDR_REG_DS, (uint16_t)Regular_Exposure);
    EEPROM.write(3, regS);                   // keep the whole-second downgrade mirror in sync
    EEPROM.write(EE_ADDR_SCHEMA, SETTINGS_SCHEMA_VER);
    EEPROM.commit();
  } else {
    Regular_Exposure = eepromReadU16(EE_ADDR_REG_DS);
  }

  // Exposures are read raw, and the guard above only inspects Layer_Height, so
  // a bad byte here reached the print loop unchecked. Regular_Exposure = 0
  // makes turn_on_LED() compute 0 ms: the LED goes HIGH and LOW with no wait
  // between, so the UV never visibly lights and the print runs on in the dark
  // with no error anywhere - it just looks like "the UV LED is dead". Bytes
  // left by a firmware with a different EEPROM layout land exactly here. These
  // are the ranges the dashboard form, the backup restore and the LCD menu
  // already agree on.
  if (Base_Exposure < 5 || Base_Exposure > 60) {   // 0.17 0-3: min 5 s (was 10) for fast resins
    Base_Exposure = 35;
    EEPROM.write(2, (uint8_t)Base_Exposure);
    EEPROM.commit();
  }
  if (Regular_Exposure < 10 || Regular_Exposure > 300) {   // deciseconds (1.0-30.0 s)
    Regular_Exposure = 140;
    eepromWriteU16(EE_ADDR_REG_DS, 140);
    EEPROM.write(3, 14);   // keep the whole-second downgrade mirror in sync
    EEPROM.commit();
  }

  // VAT capacity (added in 0.9.2 at EEPROM addr 11) - older installs have
  // 0xFF there; clamp to the valid 10..40 ml range or seed the default.
  Vat_Capacity_Ml = EEPROM.read(11);
  if (Vat_Capacity_Ml < 10 || Vat_Capacity_Ml > 40) {
    Vat_Capacity_Ml = 15;
    EEPROM.write(11, 15);
    EEPROM.commit();
  }

  // NVS-backed system values: lifetime print time + web/device settings.
  loadDeviceConfig();
  readBootTelemetry();  // 0-30: reset reason + mid-print death record
  lastUiActivityMs = millis();

  delay(1000);
  // Factory-fresh boot + a backup on the SD card -> offer to restore before
  // anything else (the backup may re-enable WiFi, MQTT, boot check...).
  if (settingsWereFactoryReset && sdBackupExists()) {
    screenRestorePrompt();
    return;                     // loop() takes over at screen 426
  }
  // Power lost mid-print: a valid checkpoint on the SD card -> offer to
  // resume. 0-33: the network comes up FIRST (the prompt is just a screen,
  // loop() keeps servicing HTTP behind it) so the dashboard can show the
  // interrupted print and answer it remotely; resumeBootPending suppresses
  // the boot-update prompt so nothing competes with the resume question.
  bool resumePendingBoot = resumeLoad();
  if (resumePendingBoot) { resumeBootPending = true; powerRestoreNotifyPending = true; }  // 0.17: notify once WiFi is up
  #if ENABLE_NETWORK
  network_setup(); // SLIBBINAS WiFi + upload server (Network.ino)
  if (!resumePendingBoot && (screen == 424 || screen == 425)) return;
  #endif
  if (resumePendingBoot) {
    screenResumePrompt();
    return;                     // loop() takes over at screen 427
  }
  screen1(); // jumps to Main Menu
}

bool prepareSelectedPrintPreview() {
  resinNeedForModelMl = -1;   // a new preview invalidates the old estimate
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(22, 34);
  gfx2->print("Processing files");
  gfx2->setCursor(34, 52);
  gfx2->print("Please wait...");
  delay(500);

  layer_counter = 0;
  File entry;
  do {
    layer_counter += 100;
    FileName = foldersel_long;
    FileName += "/";
    FileName += layer_counter;
    FileName += ".png";
    entry = SD.open(FileName);
  } while(entry);
  layer_counter -= 100;

  do {
    layer_counter++;
    FileName = foldersel_long;
    FileName += "/";
    FileName += layer_counter;
    FileName += ".png";
    entry = SD.open(FileName);
  } while(entry);
  layer_counter--;

  if (layer_counter <= 0 || layer_counter > MAX_LAYER_FILES) {
    if (layer_counter <= 0) screenNoLayers(); else screen112();
    return false;
  }

  /* screen111Checked(), o ne screen111(): ji skaiciuoja sluoksnius DAR KARTA, ir
     butent tas antrasis skaiciavimas uzraso stagedLayerHeight. Tikrinant tik
     pirmaji (virsuje), kortele suklydusi tarp dvieju skaiciavimu duotu ta pacia
     skyle, kuria sis paketas ir uzdaro: „Layers: 0" su gyvu Start, o sarga case
     111 nepasileistu, nes aukstis nepasikeites (auditas 08-17). */
  return screen111Checked();
}

// ===================================================================================
// Main Loop
// ===================================================================================
/**
 * @brief Main Loop
 * Handles button inputs and UI state transitions continuously.
 */
void loop() {
  #if ENABLE_NETWORK
  network_loop(); // network uploads - only serviced while printer is idle
  sdJobRun();     // deferred delete/import - ONLY here (the idle loop), never
                  // from service windows mid-print or the motor/pause loops
  // 0-33: the dashboard answered the boot resume prompt. Only honoured while
  // the prompt is still up (screen 427) - any button press at the printer
  // consumes the prompt and remote answers become stale by design.
  if (screen == 427 && webResumeAction) {
    char wra = webResumeAction;
    webResumeAction = 0;
    if (wra == 'R') {
      resumeStartPrint = true;   // picked up by the start path below
      screen = 111;
    } else if (wra == 'L') {
      resumeRaisePlateAndDiscard();
      finishRestorePromptBoot();
    } else {                     // 'D'
      resumeClear();
      finishRestorePromptBoot();
    }
  }
  #endif
  if (handleUiTimeout()) return;
  // -----------------------------------------------------------------------------------
  // Back Button Handling
  // Only triggers if the button is pressed (LOW)
  // -----------------------------------------------------------------------------------  
  if (digitalRead(buttonBack) == LOW) {
    switch (screen) {
      case 11:
      screen1();       
        break;
      case 113:                 // cancel delete -> back to model list
      screen11();
      counter --;
      folderDown(root);
        break;
      case 111:
      screen11();
      counter --;
      folderDown(root);
        break;
      case 112:
      screen11();
      counter --;
      folderDown(root);
        break;
      /* Visur screen111Checked(), ne screen111(): KIEKVIENAS perpiesimas is naujo
         uzraso stagedLayerHeight ir tuo nuginkluoja sarga pries Start. Invariantas
         galioja visiems kvietimams, kitaip kitas auditas ras ta pati (08-17). */
      case 114:                 // low-resin warning -> back to preview
      screen111Checked();
        break;
      case 116:                 // "Resin not set" -> Back = nieko nekeiciam
      screen111Checked();       // derva lieka nepasirinkta, Start ir toliau atsisakys
        break;
      case 115:                 // "VAT refilled?" -> Back = no, start as-is
      refillAsked = true;
      startFromResin = true;
      screen = 111;
        break;
      case 12:
      screen1(); 
        break;
      case 21:
      screen1(); 
      screen2();
        break;
      case 22:
      screen1(); 
      screen2();
        break;
      case 23:
      screen1(); 
      screen2();
        break;
      case 211:
      screen21(); 
        break;
      case 212:
      screen21(); 
        break;
      case 213:
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
      case 221:
      screen21();
      screen22();
        break;
      case 222:
      screen21();
      screen22();      
        break;        
      case 223:
      screen21();
      screen22();      
        break; 
      case 2211:
      screen221();
        break;
      case 2221:
      screen221();
      screen222();      
        break;        
      case 2231:
      screen221();
      screen223();      
        break; 
      case 231:
      screen21();
      screen23();
        break;
      case 2311:
      screen21();
      screen23();
        break;
      case 31:
      screen1(); 
      screen3();
        break; 
      case 41:                  // System menu -> back to the main menu
      screen1();
      screen4();
        break;
      case 411:
      screen41();
        break;
      case 432:                 // Statistics -> System menu (selection kept)
      systemMenuShow();
        break;
      case 440:                 // group list -> back to System menu
      screen42();
        break;
      case 441:                 // item list -> back to the group list
      screenAdvancedGroups();
        break;
      case 442:                 // WiFi prompt -> Cancel: nothing was changed,
      screenAdvancedOptions();  // just return to the group's items
        break;
      case 421:                 // Update screen -> System menu, Update selected
      screen43();
        break;
      #if ENABLE_NETWORK
      case 422:                 // install-from-file screen -> back to Update
      case 4211:                // install confirmation -> Cancel, back to Update
      screen421();
        break;
      case 423:                 // temporary WiFi prompt -> cancel, back to System > Update
      screen43();
        break;
      case 424:                 // boot update prompt -> Later
        screenBootUpdateDisablePrompt();
        break;
      case 425:                 // keep boot update check enabled
        screen1();
        break;
      #endif
      case 426:                 // SD settings restore prompt -> Skip
        finishRestorePromptBoot();
        break;
      case 427:                 // power-loss resume prompt -> Discard
        resumeBootPending = false;   // gate down: the prompt is gone (audit 08-16)
        resumeClear();
        finishRestorePromptBoot();
        break;
      case 232:                 // exposure test intro -> back to Advanced
      case 2321:                // exposure test result -> Skip (no pick)
      case 23211:               // exposure test canceled -> back to Advanced
      case 2322:                // best-bar picker -> Skip (keep current)
        screenAdvancedOptions();
        break;
      case 431:                 // About -> System menu (About stays selected)
      screen44();
        break;
      case 313:                 // "Reset settings?" -> Back = nieko nedarom
      setting_item = 11;
      screen31DOWN();
        break;
      case 311:
      if(setting_item_updown == 1){
        setting_item ++;
        screen31UP();
      }
      if(setting_item_updown == 0){
        setting_item --;
        screen31DOWN();
      }
      delay(300);
        break; 
      #if ENABLE_NETWORK
      case 312:                 // WiFi Info -> back to System menu
        screen41();
        break;
      case 3121:                // Reset confirm -> cancel, back to WiFi Info
        screenWifiInfo();
        break;
      #endif
      case 3111:
      Layer_Height = EEPROM.read(1) / 100.00;
      Base_Exposure = EEPROM.read(2);
      Regular_Exposure = eepromReadU16(EE_ADDR_REG_DS);   // 0.17 0-3: deciseconds (revert to saved)
      Base_Layer = EEPROM.read(4);
      Transition_Layer = EEPROM.read(5);
      Slow_Lift_Distance = EEPROM.read(6);
      Fast_Lift_Distance = EEPROM.read(7);
      Slow_Lift_Feedrate = EEPROM.read(8);
      Fast_Lift_Feedrate = EEPROM.read(9);
      Drop_Back_Feedrate = EEPROM.read(10);
      if(setting_item_updown == 1){
        setting_item ++;
        screen31UP();
      }
      if(setting_item_updown == 0){
        setting_item --;
        screen31DOWN();
      } 
        break;
    }
    delay(200);
  }

  // -----------------------------------------------------------------------------------
  // Up Button Handling
  // -----------------------------------------------------------------------------------  
  if (digitalRead(buttonUp) == LOW) {
    switch (screen) {
      case 2:
      screen1();    
        break;
      case 111:                 // UP on preview screen -> estimate resin
      if (estimateResin()) {
        resinNeedForModelMl = (float)resinEstimateMl;  // fresh full-model need
        startFromResin = true;  // Start pressed -> print starts in OK handler
      } else
        screen111Checked();     // Back pressed -> redraw preview (Height/Time)
        break;
      case 114:                 // UP on low-resin warning -> "Refilled" shortcut
      vatMarkRefilled();
      refillAsked = true;
      startFromResin = true;    // re-run the start path (recheck passes now,
      screen = 111;             // unless the model needs more than a full VAT)
        break;
      case 427:                 // UP on resume prompt -> lift plate only (0-2)
      resumeBootPending = false;   // gate down: the prompt is gone (audit 08-16)
      resumeRaisePlateAndDiscard();
      finishRestorePromptBoot();   // continue the normal boot (network etc.)
        break;
      case 3:
      screen2();
        break;
      case 4:
      screen3();
        break;
      case 41:                  // System menu selection up (wraps)
      systemMenuUp();
        break;
      case 2322:                // UP on best-bar picker -> next option (1..8, shift-, shift+)
        expTestPickNext();
        break;
      #if ENABLE_NETWORK
      case 421:                 // UP on Update screen -> install from file
      screen422();
        break;
      #endif
      case 11:
      folderUp(root);
        break;
      case 22:
      screen21();
        break;
      case 23:
      screen22();
        break;
      case 222:
      screen221();
        break;
      case 223:
      screen222();
        break; 
      case 2211:
      manual_lift();
        break;
      case 2221:
      manual_lift();
        break;
      case 2231:
      manual_lift();
        break;      
      case 2311:
      screen2311increase();
        break;            
      case 31:
      screen31UP();
        break;
      case 311:
      screen3111increase();
        break;
      case 3111:
      screen3111increase();
        break;
      case 440:
      advancedGroupsUp();
        break;
      case 441:
      advancedOptionsUp();
        break;
    }
    delay(200);
  }

  // -----------------------------------------------------------------------------------
  // Down Button Handling
  // -----------------------------------------------------------------------------------
  if (digitalRead(buttonDown) == LOW) {
    switch (screen) {
      case 1:
      screen2();      
        break;
      case 2:
      screen3();
        break;
      case 3:
      screen4();
        break;
      case 41:                  // System menu selection down (wraps)
      systemMenuDown();
        break;
      case 11:
      folderDown(root);
        break;
      case 21:
      screen22();
        break;
      case 22:
      screen23();
        break;
      case 221:
      screen222();
        break;
      case 222:
      screen223();
        break;
      case 2211:
      manual_down();
        break;
      case 2221:
      manual_down();
        break;
      case 2231:
      manual_down();
        break;
      case 2311:
      screen2311decrease();
        break;                    
      case 31:
      screen31DOWN();
        break;
      case 311:
      screen3111decrease();
        break;
      case 3111:
      screen3111decrease();
        break;
      case 440:
      advancedGroupsDown();
        break;
      case 441:
      advancedOptionsDown();
        break;
    }
    delay(200);
  }

  // -----------------------------------------------------------------------------------
  // OK Button Handling
  // (startFromResin/webStartPrint/resumeStartPrint let non-OK flows start the
  // existing print path)
  // -----------------------------------------------------------------------------------
  if (digitalRead(buttonOK) == LOW || startFromResin || webStartPrint || resumeStartPrint) {
    // A stray button press between the resume prompt and the print start can
    // change the screen - drop the flag instead of firing OK on every pass.
    if (resumeStartPrint && screen != 111) resumeStartPrint = false;
    switch (screen) {
      case 1:
      if (SD.begin(SDCS, SD_SCK_MHZ(16))){
        root = SD.open("/");
        screen11();
        counter = 0;
        folderDown(root);                        
      }
      else{
        screen12();
      }
        break;
      case 113:                 // delete confirmed
        deleteSelectedModel();
        break;
      case 11: {
      // Long-press OK (>= 1.5 s) on a model -> delete confirmation
      {
        unsigned long okHold = millis();
        while (digitalRead(buttonOK) == LOW && millis() - okHold < 1500) delay(10);
        if (millis() - okHold >= 1500 && strlen(foldersel_long) > 0) {
          screenDeleteConfirm();
          // Wait for button release - otherwise the still-held OK would be
          // read again on the next loop() pass and instantly confirm delete
          while (digitalRead(buttonOK) == LOW) delay(10);
          break;
        }
      }
      if (selIsArchive) {
        // OK on a .sl1/.zip -> convert it to a model, then refresh the list
        // (the archive is gone, the new model folder appears)
        importSelectedArchive();
        screen11();
        counter = 0;
        folderDown(root);
        break;
      }
      prepareSelectedPrintPreview();
      }
        break;
      case 116:                 // "Resin not set" -> OK = uzsidedam gamyklini Slow
        /* Uzdedam ir GRIZTAM i peruziura, spaudinio NEPALEIDZIAM: pirstas cia
           buvo pakeltas del dervos, ne del starto, o Start turi likti atskiras
           samoningas paspaudimas. screen111Checked(), nes applyResinProfile()
           vidinis perskaiciavimas cia nesuveikia (screen == 116), o be patikros
           liktu ta pati nulio sluoksniu skyle (08-17). */
        if (applyResinProfile("slow")) screen111Checked();
        else screen111Checked();   // slow yra flash'e, tad praktiskai nepasiekiama
        /* BUTINA. Be sito laikomas OK kitame cikle butu perskaitytas jau ekrane
           111 kaip „Start", o jei ijungtas „VAT refilled?" - dar ir atsakytu i ji
           taip. Vienas ilgesnis paspaudimas ant „Slow" paleistu spaudini, nors
           sitas ekranas pazada priesingai. Rado auditas 08-17 - tai buvo mano
           paties naujo ekrano skyle, ta pati klase kaip 313 ir screenDeleteConfirm. */
        while (digitalRead(buttonOK) == LOW) delay(10);
        break;
      case 114:                 // low-resin warning -> OK = "Start anyway"
        resinWarnAccepted = true;
        startFromResin = true;  // re-enters the start path on the next pass
        screen = 111;
        break;
      case 115:                 // "VAT refilled?" -> OK = yes, mark full & start
        vatMarkRefilled();
        refillAsked = true;
        startFromResin = true;
        screen = 111;
        break;
      case 111: {
        /* Ar masina apskritai zino, kokia derva vate? Tuscias vardas lieka
           istrynus aktyvu profili arba atkurus kopija su tusciu lauku, o
           EEPROM tuo metu tebelaiko to istrinto profilio ekspozicija. Anksciau
           cia nebuvo jokios patikros - spausdinimas prasidedavo su vaiduokliska
           recepture. Resume praleidziamas TYCIA: tesiamas spaudinys jau turi
           savo receptura, o kol stovi „Resume?", dervos pasirinkti neimanoma
           (visi /api/resin-profile/* keliai atsako 409), tad blokavimas butu
           aklaviete (V rado 08-17). */
        if (!resumeStartPrint && resinProfileName.length() == 0) {
          startFromResin = false; webStartPrint = false;
          resinWarnAccepted = false; refillAsked = false;
          screenNoResin();
          break;
        }
        // "VAT refilled?" ask before every print (optional, System > Advanced).
        // The web start path asks in the browser instead (see startPrint JS).
        if (askRefillEnabled && !refillAsked && !webStartPrint && !resumeStartPrint) {
          startFromResin = false;
          screenRefillAsk();
          break;
        }
        // Low-resin pre-start check (bookkeeping estimate, no sensor). The web
        // start path (webStartPrint) confirms in the browser instead - see
        // handleApiPrintStart. A fresh model estimate (resinNeedForModelMl,
        // set by the resin screen) allows a need-vs-left comparison.
        if (!resinWarnAccepted && !webStartPrint && !resumeStartPrint) {
          float needMl = resinNeedForModelMl;
          if ((needMl >= 0 && needMl > vatRemaining()) ||
              vatRemaining() <= (float)lowResinThresholdMl) {
            startFromResin = false;
            screenLowResinWarn(needMl);
            break;
          }
        }
        /* Paskutinis patikrinimas pries pajudant: ar sluoksniu skaicius vis dar
           tos pacios dervos? Perjungus profili is narsykles tarp „paruosta" ir
           „Start" (VAT klausimas, mazos dervos ispejimas) skaicius liktu senas -
           prie 0.10 -> 0.05 butu atspausdinta tik apatine puse. Perskaiciuojam
           tik tada, kai aukstis tikrai kitas (V klausimas, 08-16). */
        // A card that misreads at exactly this moment would otherwise start a
        // print of nothing: the plate goes down and the job "finishes" with no
        // layer ever exposed. screen111Checked() daro abu dalykus kartu - jis
        // vienintelis vieta, kur perskaiciavimas ir atsisakymas nebeissiskiria.
        if (stagedLayerHeight > 0 && fabsf(stagedLayerHeight - Layer_Height) > 0.001f) {
          if (!screen111Checked()) break;
        }

        resinWarnAccepted = false;
        refillAsked = false;      // re-ask on the next print
        startFromResin = false;   // consume the resin-screen Start request
        webStartPrint = false;    // consume the web SD-manager Start request
        printStartMs = millis();  // print-hours accounting (incl. pauses)
        savePrintActiveFlag(true);  // 0-30: armed until the single print exit
        uvLedSessionMs = 0;
        homing_canceled = false;
        print_paused = false;
        print_canceled = false;
        webResumePrint = false;
        // R-cal: the plate film leaves the vat with the very first lifts, so the
        // per-print offset is charged up front - the VAT estimate (and #40 warn/
        // stop) then errs on the safe side instead of discovering it at the end.
        resinUsedMl = resinFixedMl;
        resinUsedRawMl = 0.0;     // R-cal: GEOMETRY only - the calibration input
        // ...and clear it in NVS too: the periodic checkpoint only runs every 25
        // layers, so a cut before that would resume onto the previous run sum.
        // NOT on resume - the resume branch below READS this key; zeroing it
        // here first made the checkpoint dead code (auditor find, 08-11).
        if (!resumeStartPrint) {
          sysPrefs.begin("tinymaker", false);
          sysPrefs.putFloat("printRawMl", 0.0f);
          sysPrefs.end();
        }
        resinSampledMl = 0.0;     // nothing subtracted from the VAT yet
        lowResinNotified = vatRemaining() <= (float)lowResinThresholdMl;
                                  // already low at start (user chose to print
                                  // anyway) - do not pause on the first layer
        lowResinPreWarned = false;   // 0.17 #40: re-arm the pre-warn for this print
        current_state = 0;
        phaseWaitStage = "";
        current_layer = 0;
        Position_before_pause = 0;
        Transition_Exposure = Base_Exposure * 10;   // 0.17 0-3: ramp accumulator in deciseconds
        #if ENABLE_NETWORK
        // 0-19: snapshot the model preview into RAM while the SD is still
        // free - once the print loop owns the bus, browsers get this copy.
        capturePreviewCache(30 * 1024, true);
        #endif
        // A print started from the web reaches here with the screen possibly
        // blanked by the UI timeout - and blanked it would stay: the wake
        // logic lives in loop(), which the print never returns to, while the
        // in-print button reads act on an invisible panel (a blind OK is a
        // Cancel). Blanking cannot start while busy, so waking here closes
        // the only dark path (user finding: buttons worked, screen slept).
        uiWakeScreen();

        // Power-loss resume: seed the print state from the SD checkpoint.
        // Phase 'S' (power died during homing) restarts the print normally -
        // nothing was cured yet, so the plain homing path below is correct.
        bool resuming = resumeStartPrint;
        resumeStartPrint = false;
        resumeBootPending = false;   // boot-update check may run again later
        if (resuming) {
          if (resumePhase == 0) {           // nothing loaded
            savePrintActiveFlag(false);
            #if ENABLE_NETWORK
            freePreviewCache();
            #endif
            screen1();
            break;
          }
          /* Aukstis pasikeite tarp klausimo ir Resume? Toliau einantis judesys
             skaiciuojamas is DABARTINIO aukscio: prie 0.10 -> 0.05 plokste butu
             nuleista i puse tikro aukscio, t. y. i jau isspausdinta detale (FEP,
             derva, Z). Geriau atsisakyti tesimo, nei sulauzyti. */
          if (resumeLayerHeightCm > 0 &&
              resumeLayerHeightCm != (int)lroundf(Layer_Height * 100)) {
            // Nothing started, so nothing may stay armed: the print-active flag
            // was set a few lines up and would report a crash on the next boot.
            savePrintActiveFlag(false);
            #if ENABLE_NETWORK
            freePreviewCache();   // the browser's snapshot, for a print that is not happening
            #endif
            // Back to the prompt, NOT to the menu: the plate still stands in the
            // vat, and the prompt is the only place with "lift the plate" and
            // "discard" - the two things left to do. (Changing the height back is
            // not one of them: every settings route is closed while it stands.)
            screenResumeHeightChanged();
            screenResumePrompt();
            break;
          }
          strlcpy(foldersel_long, resumeFolder, sizeof(foldersel_long));
          foldersel = String(resumeFolder);
          layer_counter = resumeTotal;
          get_motor_updown_time();
          if (resumePhase == 'S') {
            resuming = false;               // fresh start, homing included
          } else {
            current_layer = resumeLayer;
            resinUsedMl = resumeResinMl;
            // R-cal: the raw twin is checkpointed in NVS next to vatRemainingMl,
            // so it survives the cut without depending on the factor in force now.
            sysPrefs.begin("tinymaker", true);
            resinUsedRawMl = sysPrefs.getFloat("printRawMl", 0.0f);
            sysPrefs.end();
            if (!(resinUsedRawMl > 0.0)) resinUsedRawMl = 0.0;
            resinSampledMl = resumeResinMl; // NVS vat bookkeeping continues
            printStartMs = millis() - resumeElapsedSecs * 1000UL;
            uvLedSessionMs = resumeUvLedSecs * 1000UL;
            Transition_Exposure = resumeTransitionExposureSeed(resumeLayer);
          }
        }
        #if ENABLE_NETWORK
        // P-live stack BEFORE homing (V 08-14). Homing "can take minutes" (see the
        // loop below), and until this ran a browser that joins in that window had
        // nothing to draw but the flat preview PNG - so the phone showed a picture
        // while the desktop already drew the 3D. Both values it needs are final
        // here: layer_counter and, on resume, current_layer (set just above). SD is
        // still free, same as capturePreviewCache. Freed at the single print exit,
        // including the homing-abort path.
        if (!print_canceled) liveBegin(layer_counter);
        #endif
        screen1111();
        gfx2->fillRect(136, 52, 6, 16, 0x8410);
        gfx2->fillRect(146, 52, 6, 16, 0x8410);        
        screen1111_state();
        screen1111UP();
        delay(500);
        #if ENABLE_NETWORK
        network_service_window(500);
        #endif

        // -------------------------------------------------------------------------------
        // Homing Sequence (skipped on resume: homing would drive the
        // half-printed object into the vat - Resume.ino re-trusts the
        // checkpointed position instead)
        // -------------------------------------------------------------------------------
        if (resuming) {
          resumeRecoverPosition();
          digitalWrite(FAN, HIGH);
          if (screen != 11111){
            gfx2->fillRect(136, 52, 6, 16, YELLOW);
            gfx2->fillRect(146, 52, 6, 16, YELLOW);
          }
          resumeCheckpoint('E');   // stationary at the next layer's height
        } else {
        resumeWriteStart();        // 'S': a loss during homing restarts cleanly
        stepper.setCurrentPosition(0);
        stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
        stepper.enableOutputs();
        long initial_homing = 0;
        long current_position;
        unsigned long homingNetTs = 0;
        while(!digitalRead(end_stop) && !homing_canceled && !print_canceled){
          #if ENABLE_NETWORK
          // Homing can take minutes; without this, web Stop cannot reach the
          // printer until it finishes (the motor pauses imperceptibly).
          // HTTP only: the full network_loop() also runs MQTT and the Connect
          // sync, whose timeouts (up to ~8 s) froze the motor and the
          // dashboard mid-homing - likely the "timeouts while homing" the
          // maintainer kept seeing. Nothing needs publishing during homing.
          if (millis() - homingNetTs > 250) {
            homingNetTs = millis();
            network_service_http();
          }
          #endif
          stepper.moveTo(initial_homing);  // Set the position to move to
          initial_homing--;  // Decrease by 1 for next move if needed
          stepper.run();  // Start moving the stepper
          current_position = stepper.currentPosition();
          if (current_position < -106799){
            stepper.disableOutputs();
            homing_canceled = true;
            gfx2->fillScreen(BLACK);   // the 150x70 box left a ring of the old
                                       // screen peeking at the edges (user
                                       // finding: "text sticking out")
            gfx2->fillRoundRect(5, 5, 150, 70, 7, BLACK);
            gfx2->fillRoundRect(7, 7, 146, 66, 5, RED);
            gfx2->fillRoundRect(9, 9, 142, 62, 3, BLACK);
            gfx2->fillRoundRect(16, 11, 5, 10, 1, RED);
            gfx2->fillCircle(18, 25, 2, RED);
            gfx2->setTextColor(WHITE);
            // Pin the font: this box inherits whatever the interrupted screen
            // used - a larger/other font pushed the text out of the frame
            // (user finding 07-22).
            gfx2->setFont(&FreeSans8pt7b);
            gfx2->setTextSize(1);
            gfx2->setCursor(27, 23);
            gfx2->println("Homing error,");
            gfx2->setCursor(13, 41);
            gfx2->println("print canceled."); 
            gfx2->fillRoundRect(82, 51, 67, 18, 2,  0x879F);
            gfx2->setCursor(100, 64);
            gfx2->println("OK :(");
            while(digitalRead(buttonOK) == HIGH);
            break;  
          }
          if (Duration >= 500 && screen == 1111 && digitalRead(buttonOK) == LOW) {
            screen11111();
            startTime = millis();
          }
          Duration = millis()-startTime;
          if (Duration >= 500 && screen == 11111 && digitalRead(buttonOK) == LOW){
            stepper.disableOutputs();
            homing_canceled = true;
            break;
          }
          if (Duration >= 500 && screen == 11111 && digitalRead(buttonBack) == LOW){
            screen1111();
            gfx2->fillRect(136, 52, 6, 16, 0x8410);
            gfx2->fillRect(146, 52, 6, 16, 0x8410);            
            screen1111_state();
            screen1111UP();
          }
        }
        delay(50);

        // Stop pressed during homing: retrace the descent instead of freezing
        // where it stands (user finding: "after Stop it doesn't go back").
        // The final lift cannot help here - it targets an absolute height, and
        // mid-homing there is no absolute reference: position 0 only means
        // "where the plate stood when Start was pressed", the endstop has not
        // been reached, so the true height is unknown. Returning to 0 needs no
        // reference at all: it is the same steps, walked backwards, to a place
        // the plate occupied seconds ago. Only ever upwards, and only as far
        // as it came down. A real homing failure (the endstop never arrived,
        // homing_canceled set by the travel limit) is deliberately left where
        // it is - that machine has a fault, not a cancelled print.
        if (print_canceled && homing_canceled && stepper.currentPosition() < 0) {
          stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
          stepper.enableOutputs();
          stepper.moveTo(0);
          unsigned long svc = millis();
          while (stepper.distanceToGo() != 0) {
            stepper.run();
            if (millis() - svc >= 200) {
              svc = millis();
              #if ENABLE_NETWORK
              network_service_http();
              #endif
            }
          }
          stepper.disableOutputs();
        }

        if (homing_canceled != true){
          stepper.disableOutputs();
          stepper.setCurrentPosition(0);
          digitalWrite(FAN, HIGH);
          if (screen != 11111){
            gfx2->fillRect(136, 52, 6, 16, YELLOW);
            gfx2->fillRect(146, 52, 6, 16, YELLOW);
          }
          resumeCheckpoint('E');   // homed: at layer 1's exposure height
        }
        }

        // P-live stekas paruostas dar PRIES hominga (zr. auksciau), o CIA jis atrakinamas
        // atidavimui: homing'as baigtas. Tai NEREISKIA, kad motoras visai stovi - HTTP
        // aptarnaujamas ir pauzes/atsaukimo liftu cikluose - bet ten zingsniai tik
        // trukteli, o sluoksnio atplesimas HTTP visai neaptarnauja (auditas 08-14).
        #if ENABLE_NETWORK
        // Ne atsaukimo kelyje: po jo dar eina lift_finished_print() - kelios desimtys
        // sekundziu motoro darbo, HTTP aptarnaujamas kas 200 ms, o liveClear() tik gale.
        // Atrakinus cia, 29 KB siuntinys pakliutu kaip tik i ta judesi (auditas 08-14).
        if (!homing_canceled && !print_canceled) liveReady = true;
        #endif

        // -------------------------------------------------------------------------------
        // Printing Loop
        // -------------------------------------------------------------------------------
        while(!homing_canceled && !print_canceled){
          estimated_seconds = 0;
          estimated_hours = 0;
          estimated_minutes = 0;
          motor_updown_time_total = 0;
          if (current_layer < Base_Layer)
            estimated_seconds += (Base_Layer - current_layer) * Base_Exposure;                
          estimated_seconds += (layer_counter - current_layer) * Regular_Exposure / 10;   // 0.17 0-3: ds -> s            
          motor_updown_time_total += (layer_counter - current_layer - 1) * motor_updown_time;            
          estimated_seconds += motor_updown_time_total;             
          estimated_hours = estimated_seconds / 3600;
          estimated_minutes = (estimated_seconds % 3600) / 60;
                        
          print_next_png();
          #if ENABLE_NETWORK
          network_service_window(160);
          #endif

          // VAT bookkeeping: subtract this layer's cured volume; checkpoint to
          // NVS every 25 layers so a power loss costs little (flash-wear-friendly)
          vatRemaining();
          vatRemainingMl -= (float)(resinUsedMl - resinSampledMl);
          resinSampledMl = resinUsedMl;
          if (vatRemainingMl < 0) vatRemainingMl = 0;
          if (current_layer % 25 == 0) {
            saveVatRemaining();
            sysPrefs.begin("tinymaker", false);  // 0-30: crash-record checkpoint
            sysPrefs.putUShort("prLayer", current_layer);
            sysPrefs.putULong("prEpoch", telemetryEpochNow());  // ~time of death
            sysPrefs.end();
          }

          #if ENABLE_NETWORK
          if (uiDimmedPrint) {
            // 0-22: dimmed - refresh the saver's progress line instead of
            // repainting the bright info block over it.
            drawPrintSaver(uiSaverPos);
          } else
          #endif
          if (screen != 11111 && screen != 11112){
            gfx2->fillRoundRect(2, 38, 116, 40, 3, BLACK);
            gfx2->setFont(&FreeSans8pt7b);
            gfx2->setTextColor(WHITE);
            gfx2->setTextSize(1);    
            gfx2->setCursor(6, 54);
            gfx2->print(current_layer);      
            gfx2->print(" / ");
            gfx2->print(layer_counter);
            gfx2->setCursor(6, 74);
            gfx2->print(estimated_hours);
            gfx2->print("h");
            gfx2->print(estimated_minutes);
            gfx2->print("m ");
            gfx2->setTextColor(0x879F);   // live cured-resin counter (matches screen1111)
            gfx2->print(resinUsedMl, 1);
            gfx2->print("ml");
            gfx2->setTextColor(WHITE);
          }
          
          if (current_state != 4 && current_state != 5){
            current_state = 1;
            screen1111_state();
          }
                  
          turn_on_LED();          
          gfx1->fillScreen(BLACK);
          #if ENABLE_NETWORK
          network_service_window(160);
          #endif
          
          if (current_state != 4 && current_state != 5){
            current_state = 2;
            screen1111_state();
          }
          // Layer cured, peel begins. 0.17 1-38b: capture the exact cycle base
          // (this layer's pre-lift height) once; lift_print()/lower_print() then
          // write granular 'M' checkpoints during the ~9s motion (first one on
          // entry, replacing the old single pre-lift 'M'). `pos` = this base
          // everywhere in the cycle (drift-free target); `live` tracks the real
          // height so a mid-motion loss recovers sub-mm instead of to the low end.
          if (!print_canceled) resumeCycleBaseSteps = stepper.currentPosition();
          // The service window moved from after the move to before it: a
          // pending status poll now answers "Lifting" with the countdown
          // ahead of it, not after the phase already ended. The measured
          // duration includes the window - so does next layer's, so the
          // prediction stays honest.
          // Laukimo ivertis (stabdymas/pauze) yra ATSKIRAS skaicius: jei sluoksnio
          // faze ji perrasytu, pulto juostele viduryje nusiristu atgal ir zmogus
          // matytu antra pranesima (V 08-18).
          if (current_state != 4 && current_state != 5) {
            phaseStartMs = millis();
            phaseTotalMs = prevLiftMs;
            phaseWaitStage = "";
          }
          // Matuojam nuo SAVO zymes, ne nuo phaseStartMs: pastarasis dabar gali
          // priklausyti laukimo ivertiui, ir kito sluoksnio prognoze butu sarmata.
          unsigned long liftT0 = millis();
          #if ENABLE_NETWORK
          network_service_window(160);
          #endif
          lift_print();
          prevLiftMs = millis() - liftT0;
          delay(50);
          
          if(current_layer == layer_counter)
            break;

          #if ENABLE_NETWORK
          // 0.17 #40 (V 08-08): low-resin WARNING level. Fire once when the vat
          // drops to lowResinWarnMl, still ABOVE the stop level. Independent of
          // the stop checkbox - the warning is always useful; the stop is separate.
          // ml-based (not time): the vat is small (~15 ml) and per-layer use tiny,
          // so a time trigger never fired for small models. Message carries a
          // rough runway (~layers to stop, ~min) as context. Safe: motor idle here.
          if (!lowResinPreWarned && !lowResinNotified && !print_paused && !print_canceled &&
              vatRemainingMl <= (float)lowResinWarnMl && vatRemainingMl > (float)lowResinThresholdMl) {
            lowResinPreWarned = true;
            int preMinsLeft = 0;   // 0 = too early for a rate; the message omits it
            // R-cal: resinUsedMl carries the one-off plate-film offset - only the
            // per-layer part may be divided by the layer count.
            double preLayerMl = resinUsedMl - (double)resinFixedMl;
            if (current_layer >= 5 && preLayerMl > 0.0) {
              double preRate = preLayerMl / current_layer;                // ml per layer so far
              if (preRate > 0.0) {
                int preLayersLeft = (int)((vatRemainingMl - (float)lowResinThresholdMl) / preRate);  // layers to stop
                float preLayerSecs = printStartMs ? ((millis() - printStartMs) / 1000.0f) / current_layer : 0.0f;
                preMinsLeft = (int)(preLayersLeft * preLayerSecs / 60.0f);
              }
            }
            tgNotifyLowResinSoon(vatRemainingMl, preMinsLeft);
          }
          #endif
            
          // Low resin: pause between layers (reuses the normal pause flow).
          // Fires once per threshold crossing; "VAT refilled" re-arms it.
          bool lowResinPauseNow = false;
          if (lowResinPauseEnabled && !lowResinNotified && !print_paused &&
              !print_canceled && vatRemainingMl <= (float)lowResinThresholdMl) {
            lowResinNotified = true;
            lowResinPauseNow = true;
            print_paused = true;
          }

          // -----------------------------------------------------------------------------
          // Pause Handling
          // -----------------------------------------------------------------------------
          if(print_paused == true){
            #if ENABLE_NETWORK
            if (uiDimmedPrint) {   // 0-22: a pause (web/low-resin) wakes the screen
              uiDimmedPrint = false;
              screen1111();
            }
            #endif
            Position_before_pause = stepper.currentPosition();
            stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
            stepper.enableOutputs();
            if (Position_before_pause + (pauseLiftMm * steps_mm) <= max_height * steps_mm)
              stepper.move(pauseLiftMm * steps_mm);
            else
              stepper.moveTo(max_height * steps_mm);
            #if ENABLE_NETWORK
            // Phase countdown for the dashboard ("Pausing - ~Ns"): publish the
            // lift's estimated duration; polls are answered during the move
            // (below), so every browser picks it up and ticks it down locally.
            current_state = 5;   // pausing (a button pause arrives with the layer's phase state)
            phaseStartMs = millis();
            phaseTotalMs = (unsigned long)(labs(stepper.distanceToGo()) * 60000.0 /
                           (Fast_Lift_Feedrate * steps_mm));
            phaseWaitStage = "pauseLift";   // antras pauzes etapas: kyla plokste
            #endif
            {
              // Answer HTTP every 200ms DURING the lift (the homing-return
              // pattern) - one pre-move window was not enough, a 2s poll loop
              // rarely hit it and both browsers looked frozen (user finding).
              unsigned long svc = millis();
              while (stepper.distanceToGo()!= 0) {
                stepper.run();
                if (millis() - svc >= 200) {
                  svc = millis();
                  #if ENABLE_NETWORK
                  network_service_http();
                  #endif
                }
              }
            }
            stepper.disableOutputs();
            delay(10); 

            current_state = lowResinPauseNow ? 10 : 6;  // 10 = "Refill VAT" pause
            phaseWaitStage = "";   // laukimas baigesi - stovim, skaiciuoti nebera ko
            bool lowResinNotifyPending = lowResinPauseNow;
            lowResinPauseNow = false;
            saveVatRemaining();   // checkpoint at the pause point
            resumeCheckpoint('P');  // parked position is exact
            screen1111_state();
            gfx2->fillRect(136, 12, 16, 16, RED);
            gfx2->fillTriangle(136, 52, 136, 68, 152, 60, GREEN);
            screen1111DOWN();
            #if ENABLE_NETWORK
            // Notify only after the checkpoint is saved and the pause UI is
            // drawn: on weak WiFi the blocking send can hold the loop for
            // seconds, and nothing print-critical may wait on it.
            if (lowResinNotifyPending) tgNotifyLowResin();
            #endif
              
            while(print_paused == true){
              #if ENABLE_NETWORK
              network_loop();
              #endif
              Duration2 = millis()-startTime2;
              if (Duration2 >= 500 && digitalRead(buttonUp) == LOW && screen == 1112){
              screen1111UP();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonDown) == LOW && screen == 1112){
              screen1111DOWN();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 1 && screen != 11111){
              screen11111();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 0 && screen != 11113){
              screen11113();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11111){
              screen1111();
              screen1111_state();
              screen1112();
              screen1111UP();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11113){
              screen1111();
              screen1111_state();
              screen1112();
              screen1111DOWN();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11111){
              screen1111();
              current_state = 4;
              screen1111_state();
              screen1111UP();
              print_canceled = true;
              publishStopEstimate();
              print_paused = false;
              }  
              if ((Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11113) || webResumePrint){
              webResumePrint = false;
              screen1111();
              current_state = 7;
              screen1111_state();           
              gfx2->fillRect(136, 12, 16, 16, 0x8410);
              gfx2->fillRect(136, 52, 6, 16, 0x8410);
              gfx2->fillRect(146, 52, 6, 16, 0x8410);
              gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);
              stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
              stepper.enableOutputs();
              stepper.moveTo(Position_before_pause);
              #if ENABLE_NETWORK
              // Phase countdown for the dashboard ("Resuming - ~Ns"): publish
              // the travel's estimated duration; polls are answered during the
              // move (below), so every browser picks it up and ticks locally.
              phaseStartMs = millis();
              phaseTotalMs = (unsigned long)(labs(stepper.distanceToGo()) * 60000.0 /
                             (Fast_Lift_Feedrate * steps_mm));
              phaseWaitStage = "resume";
              #endif
              {
                // Same as the pause lift: answer HTTP every 200ms during the
                // travel so the dashboards keep polling and the countdown shows.
                unsigned long svc = millis();
                while (stepper.distanceToGo()!= 0) {
                  stepper.run();
                  if (millis() - svc >= 200) {
                    svc = millis();
                    #if ENABLE_NETWORK
                    network_service_http();
                    #endif
                  }
                }
              }
              stepper.disableOutputs();
              delay(10);
              // Back at the post-lift height; the drop to the next layer
              // follows - same uncertainty window as a normal peel cycle.
              resumeCheckpointAt('M', Position_before_pause -
                  (long)((Slow_Lift_Distance + Fast_Lift_Distance) * steps_mm));
              gfx2->fillRect(136, 12, 16, 16, RED);
              gfx2->fillRect(136, 52, 6, 16, YELLOW);
              gfx2->fillRect(146, 52, 6, 16, YELLOW); 
              gfx2->drawRoundRect(128, 44, 32, 32, 3, WHITE);
              print_paused = false;    
              }       
            }                     
          }
          
          if (!print_canceled){
            current_state = 3;
            screen1111_state();
            // Sargos cia NEREIKIA (ir jos buvimas melavo): `current_state` ka tik
            // priskirtas 3 eilute aukciau, o pauze, paspausta leidziantis, savo
            // ivertį paskelbia veliau - pauzes blokas guli TARP pakelimo ir sio.
            phaseStartMs = millis();
            phaseTotalMs = prevDropMs;
            phaseWaitStage = "";
            unsigned long dropT0 = millis();
            #if ENABLE_NETWORK
            network_service_window(160);
            #endif
            lower_print();
            prevDropMs = millis() - dropT0;
            resumeCheckpoint('E');  // settled at the next layer's height
          }
        }
        #if ENABLE_NETWORK
        // Canceled: tell the phone NOW - the decision is final and the run
        // time is known, while the lift below takes tens of seconds. Finished
        // stays after the lift: that message means "come peel the print".
        bool cancelNotified = false;
        if (print_canceled || homing_canceled) { tgNotifyCanceled(); cancelNotified = true; }
        #endif
        if (!homing_canceled){
          if (!print_canceled){
            current_state = 8;
            screen1111_state();
            gfx2->fillRect(136, 12, 16, 16, 0x8410);
            gfx2->fillRect(136, 52, 6, 16, 0x8410);
            gfx2->fillRect(146, 52, 6, 16, 0x8410);
            if(printing_item_updown == 1)
              gfx2->drawRoundRect(128, 4, 32, 32, 3, 0x8410);
            if(printing_item_updown == 0)
              gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);            
          } 
          lift_finished_print();
        }
        digitalWrite(FAN, LOW);
        uiDimmedPrint = false;       // 0-22: never leave the saver armed past the print
        lastUiActivityMs = millis();
        // Vidine busena nusivalo CIA, o ne tik kito spaudinio pradzioje: iki siol
        // po stabdymo `current_state` likdavo 4, tad busena sakydavo „stopping":true
        // net stovint Idle - isamatuota 08-18. Pultas is to lipdo laukimo pranesima.
        current_state = 0;
        phaseWaitStage = "";
        savePrintTime();   // single exit point: finish, cancel and homing-abort
        savePrintActiveFlag(false);  // 0-30: clean exit - no crash record
        saveVatRemaining();
        saveLastPrintRaw();          // R-cal: this print is the calibration reference
        resumeClear();     // the checkpoint only outlives an unfinished print
        #if ENABLE_NETWORK
        freePreviewCache();          // 0-19: the RAM preview lives only for the print
        liveClear();                 // P-live: free the per-print silhouette stack
        #endif
        #if ENABLE_NETWORK
        // A homing abort/error arrives here with print_canceled still false -
        // it must never read as a finished print on the user's phone. A cancel
        // pressed DURING the final lift lands here un-notified - catch it.
        if (print_canceled || homing_canceled) { if (!cancelNotified) tgNotifyCanceled(); }
        else                                   tgNotifyFinished();
        #endif
        screen1();
        #if ENABLE_NETWORK
        tinymakerConnectSchedulePrintSync();
        #endif
      }
        break;
      
      case 12:
      if (SD.begin(SDCS, SD_SCK_MHZ(16))){
        root = SD.open("/");
        screen11();
        counter = 0;
        folderDown(root);                        
      }
      else{
        screen12();
      }
        break;
      case 2:
      screen21();
        break;
      case 21:
      screen211();
        break;
      case 211:
      screen212();
        break;
      case 212:
      screen213();
        break;
      case 213:
      screen214();
        break;
      case 22:
      screen221();
        break;
      case 221:
      screen2211();
        break;
      case 222:
      screen2221();
        break;
      case 223:
      screen2231();
        break; 
      case 23:
      screen231();
        break;
      case 231:
      screen2311();
        break; 
      case 2311:
      screen23111();
        break;        
      case 3:
      setting_item = 1;
      screen31UP();
        break;
      case 31:
        screen311();
        break;
      case 4:
        screen41();
        break;
      case 41:                     // System menu OK - dispatch by selection
        if (system_item == 1) {
          #if ENABLE_NETWORK
          screenWifiInfo();
          #else
          screen411();
          #endif
        } else if (system_item == 2) {
          advanced_group = 1;      // 0-17a: Advanced opens the group list
          screenAdvancedGroups();
        } else if (system_item == 3) {
          screen432();             // Statistics (0-17b)
        } else if (system_item == 4) {
          #if ENABLE_NETWORK
          if (!wifiEnabled && !wifiTemporarilyEnabled) screenUpdateWifiConfirm();
          else screen421();
          #else
          screen421();
          #endif
        } else {
          screen431();             // About
        }
        break;
      case 440:                    // enter the selected group's items
        advanced_item = 1;
        screenAdvancedOptions();
        break;
      #if ENABLE_NETWORK
      case 312:                 // WiFi Info -> open Reset WiFi confirmation
        screenWifiResetConfirm();
        break;
      case 3121:                // Confirmed -> erase credentials + reboot
        wifiDoReset();
        break;
      case 423:                 // Temporarily enable WiFi, then open Update
        wifiTemporarilyEnabled = true;
        webDashboardTemporarilyEnabled = true;
        // 0-35 (GitHub #38): if WiFi was off at boot, network_setup() already
        // latched networkStarted and returned early. Clear it so this call
        // re-inits the stack and actually brings WiFi up for the self-update.
        networkStarted = false;
        network_setup();
        screen421();
        break;
      case 424:                 // boot update prompt -> Install
        if (otaHasUpdate()) otaInstallLatest();
        break;
      case 425:                 // disable boot update check
        bootUpdateCheckEnabled = false;
        saveDeviceConfig();
        screen1();
        break;
      case 421:                 // Update screen -> confirm before installing
        if (otaHasUpdate()) screenUpdateConfirm();
        break;
      case 4211:                // install confirmation -> Install
        otaInstallLatest();
        break;
      #endif
      case 426:                 // SD settings restore prompt -> Restore
        screenRestoreDone(restoreFromSdBackup());
        finishRestorePromptBoot();
        break;
      case 427:                 // power-loss resume prompt -> Resume the print
        resumeBootPending = true;   // network boots quietly (no update prompt)
        // Same guard as finishRestorePromptBoot: a still-held button must not
        // reach network_setup's "hold BACK = erase WiFi" emergency check.
        while (digitalRead(buttonBack) == LOW || digitalRead(buttonOK) == LOW ||
               digitalRead(buttonUp) == LOW || digitalRead(buttonDown) == LOW) {
          delay(10);
        }
        #if ENABLE_NETWORK
        network_setup();
        #endif
        resumeStartPrint = true;    // picked up by the start path below
        screen = 111;
        break;
      case 232:                 // exposure test intro -> Start
        runExpTest();
        break;
      case 2321:                // exposure test result -> Pick best bar
        expTestPickStart();
        break;
      case 23211:               // exposure test canceled -> back to Advanced
        screenAdvancedOptions();
        break;
      case 2322:                // best-bar picker -> Set (apply the pick)
        expTestApplyPick();
        break;
      case 441:
        advancedOptionsSelect();
        break;
      case 442:                 // WiFi prompt -> Reboot: apply the toggle now
        applyWifiToggleAndReboot();
        break;
      case 313:                 // "Reset settings?" -> Reset (OK)
        resetEverythingToFactory();
        saveDeviceConfig();     // kvieciancio reikalas (zr. funkcijos komentara)
        /* Debesu kopija cia NEplanuojama samoningai: planas gyvena RAM'e su 3 s
           delsa (tinymakerConnectScheduleBackup), o mes po 1,2 s perkraunam - jis
           nespetu isvykti. Nustatymai jau NVS, o debesu kopija atsinaujins per
           pirma kita pakeitima. Web kelias planuoja, nes ten perkrovimo nera.
           Perkraunam. Atstatymas grazina ir WiFi bei pulto jungiklius, o jie
           isijungia tik per paleidima: be perkrovimo ekranas sakytu „ijungta",
           o radijas liktu isjunges - butent tokia nesutaptis atsirado, kai sis
           mygtukas gavo irenginio nustatymus (mano paties analize 08-16).
           Web kelio tai neliecia: ten perkrovimas nutrauktu atsakyma, o su
           isjungtu WiFi i pulta apskritai nepatektum. */
        gfx2->fillScreen(BLACK);
        uiFrame(ORANGE);
        gfx2->setFont(&FreeSans8pt7b);
        gfx2->setTextColor(WHITE);
        gfx2->setTextSize(1);
        gfx2->setCursor(8, 21);
        gfx2->print("Settings reset.");
        gfx2->setCursor(8, 43);
        gfx2->print("Restarting...");
        delay(1200);
        ESP.restart();
        break;
      case 311:
      if(setting_item_updown == 1){
        setting_item ++;
        screen31UP();
      }
      if(setting_item_updown == 0){
        setting_item --;
        screen31DOWN();
      }
        break; 
      case 3111:
      savePrintSettings();
      if(setting_item_updown == 1){
        setting_item ++;
        screen31UP();
      }
      if(setting_item_updown == 0){
        setting_item --;
        screen31DOWN();
      } 
        break;
    }
    delay(200);
  } 
}
