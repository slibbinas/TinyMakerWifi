// ---- Resin profiles (/resin/*.json) ---------------------------------------
// 0.17 0-16. A profile is nothing but a saved set of settings the firmware
// already has: applying one copies its values into the live variables and saves
// them the ordinary way (savePrintSettings + saveDeviceConfig). Nothing else in
// the firmware needs to know profiles exist - printing, the time estimate and
// the ml bookkeeping keep reading the same variables as before.
//
// Three origins, in list order:
//   1. two BUILT-IN profiles (Fast / Slow) that live in flash, so the picker is
//      never empty - no card, no network, after a factory reset;
//   2. an overlay file with a built-in's slug (/resin/fast.json) - written when
//      the user edits a built-in, and takes precedence over the flash values.
//      "Reset to factory" simply deletes it;
//   3. everything else in /resin - installed from the gh-pages library or saved
//      by the user.
//
// Kept out of the ENABLE_NETWORK block so the on-device menu works in the
// network-free build too; the HTTP endpoints and dashboard card live in
// Network.ino.
// RESIN_MAX_PROFILES and struct ResinProfileValues live in TinyMaker.ino, next
// to the prototypes - Interface.ino and Network.ino need them and come first.
#define RESIN_DIR "/resin"
#define RESIN_JSON_MAX 1024      // a profile is ~200 B; anything larger is not ours

struct ResinBuiltin {
  const char *slug;
  const char *display;
  const char *testedBy;   // "" = nobody has printed with it on a TinyMaker yet
  const char *testedOn;
  const char *buyUrl;     // "" = no link
  ResinProfileValues v;
};

// Only two generic starting points live in flash, so the picker is never empty:
// no card, no network, after a factory reset. NAMED resins (SUNLU, Anycubic...)
// belong to the gh-pages library instead (V 08-16) - that way a new resin needs
// a published file, not a firmware release, and the download path gets exercised
// by the resins we actually ship.
//
// Fast carries values MEASURED on this printer (08-07 exposure test, 08-09
// weighing, R-cal fit); Slow is the factory set. Manufacturer datasheets are
// never used - this machine's colour TFT absorbs a lot of UV, so a "2-3 s" resin
// wants roughly 8-15 s here. Movement values are the factory set in both: no
// measurement says a fast resin wants a different lift.
static const ResinBuiltin RESIN_BUILTIN[] = {
  { "fast", "Fast resin", "", "", "",
    { 0.05f, 18, 80,  4, 5, 1, 2, 40, 50, 50, 1.157f, 1.092f, 0.39f, -1, -1, -1, -1 } },
  { "slow", "Slow resin (factory)", "", "", "",
    // 0.10 mm, not 0.05: this profile IS resetSettingsToDefault() (EEPROM addr 1
    // = 10), and the two have to agree or the name lies about the machine.
    { 0.10f, 35, 140, 2, 5, 1, 2, 40, 50, 50, 1.100f, 1.000f, 0.00f, -1, -1, -1, -1 } },
};
#define RESIN_BUILTIN_COUNT ((int)(sizeof(RESIN_BUILTIN) / sizeof(RESIN_BUILTIN[0])))

// -1 when the slug is not one of the built-ins.
int resinBuiltinIndex(const String &name) {
  for (int i = 0; i < RESIN_BUILTIN_COUNT; i++)
    if (name == RESIN_BUILTIN[i].slug) return i;
  return -1;
}

String resinProfilePath(const String &name) {
  return String(RESIN_DIR) + "/" + name + ".json";
}

/* Kas gali tiekti „Buy" nuoroda. Viena vieta abiem keliams: irasant per
   /api/resin-profile/save IR skaitant faila is korteles. Anksciau skaitymo
   kelias tikrino tik „https://", tad kazkieno paruosta kortele galejo idejti i
   pulta melyna nuoroda, atrodancia kaip printerio rekomendacija (saugumo
   auditas, LOW, 08-17). Pabaigos „/" butinas: be jo pratektu
   „https://tinymakerwifi.com.evil.tld/" ir „https://tinymakerwifi.com@evil.tld/". */
bool resinBuyUrlAllowed(const String &u) {
  return u.startsWith("https://tinymakerwifi.com/") ||
         u.startsWith("https://slibbinas.github.io/");
}

bool resinProfileFileExists(const String &name) {
  if (name.length() == 0) return false;
  File f = SD.open(resinProfilePath(name).c_str());
  bool ok = (bool)f;
  if (f) f.close();
  return ok;
}

// Fill out[] with the .json basenames in /resin, built-in slugs skipped (they
// are listed from flash instead, with the overlay merged in). Returns count.
int listResinProfileFiles(String out[], int maxN) {
  int count = 0;
  File dir = SD.open(RESIN_DIR);
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
    if (!lower.endsWith(".json")) continue;
    String slug = s.substring(0, s.length() - 5);
    // Our own scratch files (rename writes through __rn_tmp) are not profiles:
    // one left behind by a card failure used to show up in the picker and on the
    // printer's own screen as if someone had made it (sixth audit, 08-17).
    if (slug.startsWith("__")) continue;
    if (resinBuiltinIndex(slug) >= 0) continue;   // overlay, not a separate entry
    out[count++] = slug;
  }
  dir.close();
  return count;
}

// The full picker order: built-ins first, then the card.
int listResinProfiles(String out[], int maxN) {
  int count = 0;
  for (int i = 0; i < RESIN_BUILTIN_COUNT && count < maxN; i++)
    out[count++] = RESIN_BUILTIN[i].slug;
  if (count < maxN) count += listResinProfileFiles(out + count, maxN - count);
  return count;
}

bool resinProfileExists(const String &name) {
  return resinBuiltinIndex(name) >= 0 || resinProfileFileExists(name);
}

bool resinProfileReadJson(const String &name, String &json) {
  File f = SD.open(resinProfilePath(name).c_str());
  if (!f) return false;
  json = "";
  uint32_t sz = f.size();
  if (sz >= RESIN_JSON_MAX) { f.close(); return false; }
  json.reserve(sz + 1);
  while (f.available() && json.length() < RESIN_JSON_MAX) json += (char)f.read();
  f.close();
  return json.length() > 0;
}

// Everything the callers need about one profile, from ONE read of the file:
// its values, its display name and whether a built-in has been edited. Reading
// the same file three times (values / name / exists) turned a 16-profile list
// into ~50 SD opens per request.
//
// The built-in defaults come first where there are any, then every key the file
// actually carries, each clamped to the same range /api/config enforces. A
// missing or unreadable key leaves that setting alone, so a truncated or
// hand-mangled file can never push a value out of range.
bool resinProfileInfo(const String &name, ResinProfileInfo &info) {
  int b = resinBuiltinIndex(name);
  info.builtin = (b >= 0);
  info.edited = false;
  info.hasFile = false;
  info.display = info.builtin ? String(RESIN_BUILTIN[b].display) : slugToTitle(name);
  bool known = false;
  // Every field needs a sane starting value BEFORE the file is read: a card
  // profile has no built-in defaults to fall back on, and the file only carries
  // the keys it happens to have. Without this, "a missing key leaves that
  // setting alone" left uninitialised stack behind - and a profile saved
  // without weighings (or the library's own file, which has none) would hand
  // garbage to resinFitCalibration() and on into NVS.
  if (info.builtin) {
    info.v = RESIN_BUILTIN[b].v;
    info.meta.testedBy = RESIN_BUILTIN[b].testedBy;
    info.meta.testedOn = RESIN_BUILTIN[b].testedOn;
    info.meta.buyUrl = RESIN_BUILTIN[b].buyUrl;
    known = true;
  } else {
    /* Sekla NE is gyvu nustatymu. Failas, kuriame trukstamas koks nors raktas,
       tada paveldedavo ta reiksme, kuri atsitiktinai ikelta klausimo metu: tas
       PATS failas duodavo skirtingus skaicius priklausomai nuo aktyvios dervos,
       A -> B -> A negrazindavo pradiniu reiksmiu, o pulto palyginimas „reiksmes
       ekrane skiriasi" lygino su judanciu taikiniu. Gamyklinis „slow" - pastovus
       atskaitos taskas (auditas 08-17). Musu bibliotekos failai turi visus
       raktus, tad jiems niekas nesikeicia. */
    int sb = resinBuiltinIndex("slow");
    info.v = RESIN_BUILTIN[sb >= 0 ? sb : 0].v;
  }
  // Samples come from the file or not at all - never inherited from whatever
  // resin happens to be loaded right now.
  info.v.calRawA = info.v.calGramsA = info.v.calRawB = info.v.calGramsB = -1;

  String json;
  if (!resinProfileReadJson(name, json)) return known;
  info.hasFile = true;          // there is something on the card to delete
  info.edited = info.builtin;   // the file shadows the flash values
  ResinProfileValues &v = info.v;
  // ...but a file that happens to hold the factory numbers is not an edit. The
  // badge (and "Reset to factory" with it) has to mean something changed, so the
  // values are compared at the end of this function, not just the file's
  // existence (audit 08-16).
  ResinProfileValues factory = info.v;

  double d = 0;
  // Only two heights physically exist here; anything else rounds the same way
  // /api/config does, so a hand-edited file cannot invent a third.
  if (readJsonNumberAny(json, "layer_height", d) && d > 0)
    v.layerHeight = d < 0.075 ? 0.05f : 0.10f;
  if (readJsonNumberAny(json, "base_exposure", d))
    v.baseExposure = constrain((long)lround(d), 5L, 60L);
  if (readJsonNumberAny(json, "regular_exposure", d))
    v.regularDs = constrain((long)lround(d * 10.0), 10L, 300L);
  if (readJsonNumberAny(json, "base_layers", d))
    v.baseLayers = (uint8_t)constrain((long)lround(d), 1L, 8L);
  if (readJsonNumberAny(json, "transition_layers", d))
    v.transitionLayers = (uint8_t)constrain((long)lround(d), 0L, 10L);
  if (readJsonNumberAny(json, "slow_lift_distance", d))
    v.slowLiftDist = (uint8_t)constrain((long)lround(d), 1L, 3L);
  if (readJsonNumberAny(json, "fast_lift_distance", d))
    v.fastLiftDist = (uint8_t)constrain((long)lround(d), 1L, 3L);
  if (readJsonNumberAny(json, "slow_lift_feedrate", d))
    v.slowLiftFeed = (int)constrain((long)lround(d), 20L, 50L);
  if (readJsonNumberAny(json, "fast_lift_feedrate", d))
    v.fastLiftFeed = (int)constrain((long)lround(d), 20L, 50L);
  if (readJsonNumberAny(json, "drop_back_feedrate", d))
    v.dropBackFeed = (int)constrain((long)lround(d), 20L, 50L);
  if (readJsonNumberAny(json, "density", d) && d >= 0.8 && d <= 2.0)
    v.density = (float)d;
  if (readJsonNumberAny(json, "cal_factor", d) &&
      d >= RESIN_CAL_MIN && d <= RESIN_CAL_MAX)
    v.calFactor = (float)d;
  if (readJsonNumberAny(json, "cal_fixed_ml", d) && d >= 0 && d <= RESIN_FIXED_MAX)
    v.fixedMl = (float)d;
  if (readJsonNumberAny(json, "cal_raw_a", d) && d > 0) v.calRawA = (float)d;
  if (readJsonNumberAny(json, "cal_grams_a", d) && d > 0) v.calGramsA = (float)d;
  if (readJsonNumberAny(json, "cal_raw_b", d) && d > 0) v.calRawB = (float)d;
  if (readJsonNumberAny(json, "cal_grams_b", d) && d > 0) v.calGramsB = (float)d;

  readJsonStringField(json, "tested_by", info.meta.testedBy);
  readJsonStringField(json, "tested_on", info.meta.testedOn);
  if (readJsonStringField(json, "buy_url", info.meta.buyUrl) &&
      !resinBuyUrlAllowed(info.meta.buyUrl))
    info.meta.buyUrl = "";   // only our own catalogue, whatever the file says

  // A built-in keeps its own name. Otherwise a hand-dropped /resin/fast.json
  // with "name": "My resin" would quietly rename the flash profile, and the
  // overlay would stop looking like an overlay.
  String pretty;
  if (!info.builtin && readJsonStringField(json, "name", pretty) && pretty.length())
    info.display = pretty;

  // The badge means "these numbers are not the factory ones" - an overlay that
  // holds the factory values (or one left behind by an older factory recipe) is
  // not an edit, and offering "Reset to factory" for it says nothing true.
  if (info.edited && memcmp(&info.v, &factory, sizeof(ResinProfileValues)) == 0)
    info.edited = false;
  return true;
}

bool resinProfileValues(const String &name, ResinProfileValues &v) {
  ResinProfileInfo info;
  if (!resinProfileInfo(name, info)) return false;
  v = info.v;
  return true;
}


// Copy a profile into the live settings and persist them the ordinary way.
bool applyResinProfile(const String &name) {
  ResinProfileValues v;
  if (!resinProfileValues(name, v)) return false;
  bool heightChanged = (v.layerHeight != Layer_Height);
  long replacedBaseS = Base_Exposure, replacedRegDs = Regular_Exposure;

  Layer_Height = v.layerHeight;
  Base_Exposure = v.baseExposure;
  Regular_Exposure = v.regularDs;
  Base_Layer = v.baseLayers;
  Transition_Layer = v.transitionLayers;
  Slow_Lift_Distance = v.slowLiftDist;
  Fast_Lift_Distance = v.fastLiftDist;
  Slow_Lift_Feedrate = v.slowLiftFeed;
  Fast_Lift_Feedrate = v.fastLiftFeed;
  Drop_Back_Feedrate = v.dropBackFeed;
  // The lift settings feed a cached timing table; without this the print-time
  // estimate would keep answering with the previous profile's movement.
  get_motor_updown_time();

  // The whole resin bookkeeping travels with the profile: density, the weighed
  // samples, and the fit they produce. Two traps live here, both found in the
  // 08-16 design audit:
  //   * the samples are GRAMS of one particular resin. Left on the machine they
  //     would make the dashboard call a never-weighed resin "calibrated", and
  //     the next weighing would fit a line through two different resins;
  //   * resinFitCalibration() OVERWRITES factor/fixed from those samples, so it
  //     has to run BEFORE anything is persisted - calling it afterwards (as the
  //     density-change path does) would throw the profile's own numbers away.
  resinDensity = v.density;
  resinCalFactor = v.calFactor;
  resinFixedMl = v.fixedMl;
  calRawA = v.calRawA; calMeasA = v.calGramsA;
  calRawB = v.calRawB; calMeasB = v.calGramsB;
  calNewRaw = calNewMeas = -1;      // the "newest sample" belonged to the old resin
  if (calRawA > 0 || calRawB > 0)
    resinFitCalibration();          // re-derive factor/fixed at THIS density

  // Dashboard "Undo" offers the exposure this switch replaced. Without these
  // it offered whatever an unrelated edit left behind, and one click wrote a
  // stranger's number into the new resin (audit 08-16).
  rememberPrevBaseExposure(replacedBaseS);
  rememberPrevRegularExposure(replacedRegDs);
  // The last print's raw ml belonged to the OLD resin: left standing, the next
  // weighing would calibrate this profile against someone else's print. Picking
  // the SAME profile again (or dropping its overlay) is not a resin change, so
  // the weighing survives that (audit 08-16).
  if (name != resinProfileName) {
    lastPrintRawMl = -1;
    sysPrefs.begin("tinymaker", false);
    sysPrefs.putFloat("lastPrintMl", lastPrintRawMl);   // RAM alone would come back
    sysPrefs.end();
  }

  resinProfileName = name;
  resinProfileRev++;
  savePrintSettings();
  saveDeviceConfig();
  // A model staged for printing counted its layers with the OLD height (0.10 mm
  // pairs two 0.05 mm slices). Re-run the preview so the count, the height and
  // the time estimate match the profile now in force - otherwise Start would
  // print half the model, or run past the last slice.
  /* 111 = paruostas modelis, 114 = mazos dervos ispejimas. 113 CIA NEGALIMA:
     tai „Delete model?" patvirtinimas, ir jis tyliai virstu print preview su
     [Start] po pirstu (auditas 08-16). 115 (VAT klausimas) dengia stagedLayerHeight
     sarga pries pat starta. */
  // screen111Checked(), o ne screen111(): perskaiciavimas nuginkluoja sarga pries
  // Start, tad atsisakymas privalo keliauti kartu su juo (auditas 08-17).
  /* 116 („Resin not set") CIA BUTINAS: be jo pasirinkus derva is pulto printerio
     ekranas toliau sakytu „Resin not set", o jo desinys mygtukas perrasytu ka tik
     pasirinkta derva gamykliniu Slow. Perpiesiam ir tada, kai aukstis nepasikeite -
     pasikeite pats atsakymas i klausima „ar derva pasirinkta" (auditas 08-17). */
  if (screen == 116) screen111Checked();
  else if (heightChanged && (screen == 111 || screen == 114))
    screen111Checked();
  return true;
}

// Snapshot of the settings as they are right now - what "save" means unless
// the caller hands over a set of its own.
void resinProfileFromCurrent(ResinProfileValues &v) {
  v.layerHeight = Layer_Height;
  v.baseExposure = Base_Exposure;
  v.regularDs = Regular_Exposure;
  v.baseLayers = Base_Layer;
  v.transitionLayers = Transition_Layer;
  v.slowLiftDist = Slow_Lift_Distance;
  v.fastLiftDist = Fast_Lift_Distance;
  v.slowLiftFeed = Slow_Lift_Feedrate;
  v.fastLiftFeed = Fast_Lift_Feedrate;
  v.dropBackFeed = Drop_Back_Feedrate;
  v.density = resinDensity;
  v.calFactor = resinCalFactor;
  v.fixedMl = resinFixedMl;
  v.calRawA = calRawA; v.calGramsA = calMeasA;
  v.calRawB = calRawB; v.calGramsB = calMeasB;
}

// Write a value set into /resin/<name>.json. Doubles as editing a built-in: the
// file then shadows the flash values. Taking the values as an argument is what
// lets the dashboard install a library profile without the firmware fetching
// anything - the browser reads the ~200 B file off gh-pages and posts it here.
bool writeResinProfileValues(const String &name, const String &display,
                             const ResinProfileValues &vals,
                             const ResinProfileMeta &meta) {
  if (name.length() == 0) return false;
  if (!SD.exists(RESIN_DIR) && !SD.mkdir(RESIN_DIR)) return false;

  String path = resinProfilePath(name);
  SD.remove(path.c_str());
  File f = SD.open(path.c_str(), FILE_WRITE);
  if (!f) return false;

  int b = resinBuiltinIndex(name);
  String label = display.length() ? display
                                  : (b >= 0 ? String(RESIN_BUILTIN[b].display)
                                            : slugToTitle(name));

  f.print("{\n  \"name\": \"");
  f.print(modelMetaJsonEscape(label));
  f.print("\",\n  \"layer_height\": ");
  f.print(String(vals.layerHeight, 2));
  f.print(",\n  \"base_exposure\": ");
  f.print(vals.baseExposure);
  f.print(",\n  \"regular_exposure\": ");
  f.print(String(vals.regularDs / 10.0f, 1));
  f.print(",\n  \"base_layers\": ");
  f.print(vals.baseLayers);
  f.print(",\n  \"transition_layers\": ");
  f.print(vals.transitionLayers);
  f.print(",\n  \"slow_lift_distance\": ");
  f.print(vals.slowLiftDist);
  f.print(",\n  \"fast_lift_distance\": ");
  f.print(vals.fastLiftDist);
  f.print(",\n  \"slow_lift_feedrate\": ");
  f.print(vals.slowLiftFeed);
  f.print(",\n  \"fast_lift_feedrate\": ");
  f.print(vals.fastLiftFeed);
  f.print(",\n  \"drop_back_feedrate\": ");
  f.print(vals.dropBackFeed);
  f.print(",\n  \"density\": ");
  f.print(String(vals.density, 3));
  f.print(",\n  \"cal_factor\": ");
  f.print(String(vals.calFactor, 3));
  f.print(",\n  \"cal_fixed_ml\": ");
  f.print(String(vals.fixedMl, 2));
  // The weighed samples belong to this resin - saved with it, they come back
  // with it, and the next resin starts its own pair instead of inheriting ours.
  if (vals.calRawA > 0 && vals.calGramsA > 0) {
    f.print(",\n  \"cal_raw_a\": ");   f.print(String(vals.calRawA, 2));
    f.print(",\n  \"cal_grams_a\": "); f.print(String(vals.calGramsA, 2));
  }
  if (vals.calRawB > 0 && vals.calGramsB > 0) {
    f.print(",\n  \"cal_raw_b\": ");   f.print(String(vals.calRawB, 2));
    f.print(",\n  \"cal_grams_b\": "); f.print(String(vals.calGramsB, 2));
  }
  // Provenance last: it is optional, and a profile without it simply has none.
  if (meta.testedBy.length()) {
    f.print(",\n  \"tested_by\": \"");
    f.print(modelMetaJsonEscape(meta.testedBy));
    f.print("\"");
  }
  if (meta.testedOn.length()) {
    f.print(",\n  \"tested_on\": \"");
    f.print(modelMetaJsonEscape(meta.testedOn));
    f.print("\"");
  }
  if (meta.buyUrl.length()) {
    f.print(",\n  \"buy_url\": \"");
    f.print(modelMetaJsonEscape(meta.buyUrl));
    f.print("\"");
  }
  f.print("\n}\n");
  f.close();
  resinProfileRev++;   // the LCD menu's cached label is now stale
  return true;
}

// "Save current into this profile": the live settings, keeping whatever type
// the profile already claims (a built-in keeps its own).
bool writeResinProfile(const String &name, const String &display) {
  ResinProfileValues v;
  resinProfileFromCurrent(v);
  ResinProfileMeta meta;
  ResinProfileInfo old;
  if (resinProfileInfo(name, old)) meta = old.meta;   // keep who tested it
  return writeResinProfileValues(name, display, v, meta);
}

/* Gamyklinis atstatymas nuima VISU isiutu profiliu overlay failus. Anksciau
   buvo nuimamas tik „slow", tad /resin/fast.json islikdavo ir po atstatymo Fast
   grazindavo vartotojo senus redaguotus skaicius - nors patvirtinimo ekranas
   zadejo, kad dervos profilis atsistato. Ciklas, o ne dvi eilutes, kad tretias
   isiutas profilis nebutu vel pamirstas (auditas 08-17).
   Kviecia resetEverythingToFactory(); RESIN_BUILTIN cia pat, o TinyMaker.ino jo
   nemato (jis suklijuojamas pirmas). */
void resinDropBuiltinOverlays() {
  for (int i = 0; i < RESIN_BUILTIN_COUNT; i++)
    SD.remove(resinProfilePath(RESIN_BUILTIN[i].slug).c_str());
}

// Built-in: drop the overlay and go back to the flash values. Anything else:
// delete the profile. Returns false when there is nothing to remove.
bool deleteResinProfile(const String &name) {
  if (!resinProfileFileExists(name)) return false;
  if (!SD.remove(resinProfilePath(name).c_str())) return false;
  resinProfileRev++;
  if (resinProfileName == name) {
    // A deleted built-in still exists (flash values); a deleted file does not.
    if (resinBuiltinIndex(name) >= 0) applyResinProfile(name);
    else { resinProfileName = ""; saveDeviceConfig(); }
  }
  return true;
}

// Advanced-menu cycle: Fast -> Slow -> each file -> back to Fast.
String nextResinProfile(const String &current) {
  String names[RESIN_MAX_PROFILES];
  int n = listResinProfiles(names, RESIN_MAX_PROFILES);
  if (n == 0) return "";
  for (int i = 0; i < n; i++)
    if (names[i] == current) return names[(i + 1) % n];
  return names[0];   // current was deleted or never set
}
