/**
 * Import.ino - ZIP / SL1 -> printable model conversion.
 *
 * Moved out of Network.ino so it works in ENABLE_NETWORK=0 builds too:
 * the pipeline only needs SD + unzipLIB, no WiFi. Used by both the network
 * upload (Network.ino finishUpload) and the on-device "import from SD"
 * flow (OK on a .sl1/.zip entry in the Print list).
 */

#include <unzipLIB.h>      // bitbank2 (include is unconditional on purpose)

// Separate File handle for the unzipper - do NOT reuse the global
// 'myfile' from PNG.ino (it belongs to the PNGdec callbacks).
File zipSrcFile;

// ===================================================================================
// unzipLIB <-> SdFat callbacks
// ===================================================================================
void *zipOpen(const char *filename, int32_t *size) {
  zipSrcFile = SD.open(filename);
  if (!zipSrcFile) return NULL;
  *size = zipSrcFile.size();
  return (void *)&zipSrcFile;
}

void zipClose(void *p) {
  ZIPFILE *pzf = (ZIPFILE *)p;
  File *f = (File *)pzf->fHandle;
  if (f) f->close();
}

int32_t zipRead(void *p, uint8_t *buffer, int32_t length) {
  ZIPFILE *pzf = (ZIPFILE *)p;
  File *f = (File *)pzf->fHandle;
  return f->read(buffer, length);
}

int32_t zipSeek(void *p, int32_t position, int iType) {
  ZIPFILE *pzf = (ZIPFILE *)p;
  File *f = (File *)pzf->fHandle;
  if (iType == SEEK_SET)      f->seek(position);
  else if (iType == SEEK_CUR) f->seek(f->position() + position);
  else                        f->seek(f->size() + position); // SEEK_END
  return f->position();
}

// ===================================================================================
// Helpers
// ===================================================================================

// Extract layer number from a zip entry name, or -1 if not a layer PNG.
// Accepts "17.png", "Benchy00016.png", "slice/12.png". Rejects thumbnails
// and non-png entries. Number = trailing digits right before ".png".
int layerIndexFromEntry(const char *entryName) {
  String n = String(entryName);
  n.toLowerCase();
  if (n.indexOf("thumbnail") >= 0) return -1;
  if (!n.endsWith(".png")) return -1;
  int slash = n.lastIndexOf('/');
  String base = n.substring(slash + 1, n.length() - 4); // strip dir + ".png"
  int i = base.length() - 1;
  if (i < 0 || !isDigit(base[i])) return -1;
  while (i > 0 && isDigit(base[i - 1])) i--;
  return base.substring(i).toInt();
}

// Make a safe SD folder name from an archive filename (no extension).
// Folder name buffer in the stock firmware is 101 chars; keep well under.
String safeModelName(String fn) {
  int slash = max(fn.lastIndexOf('/'), fn.lastIndexOf('\\'));
  if (slash >= 0) fn = fn.substring(slash + 1);
  int dot = fn.lastIndexOf('.');
  if (dot > 0) fn = fn.substring(0, dot);
  String out = "";
  for (unsigned int i = 0; i < fn.length() && out.length() < 40; i++) {
    char c = fn[i];
    if (isAlphaNumeric(c) || c == '-' || c == '_') out += c;
  }
  if (out.length() == 0) out = "Model";
  return out;
}

bool deleteModelFolder(const char *path, bool showProgress);

bool sdPathExists(const String &path, bool *isDir = NULL) {
  File entry = SD.open(path.c_str());
  if (!entry) return false;
  if (isDir) *isDir = entry.isDirectory();
  entry.close();
  return true;
}

uint32_t sdFileSize(const String &path) {
  File f = SD.open(path.c_str());
  if (!f) return 0;
  uint32_t s = f.size();
  f.close();
  return s;
}

bool modelSummaryFromSourceLayers(int sourceLayers, ModelSummary &summary) {
  if (sourceLayers <= 0 || sourceLayers > MAX_LAYER_FILES) return false;

  get_motor_updown_time();
  summary.sourceLayers = sourceLayers;
  summary.printLayers = sourceLayers;
  if (Layer_Height < 0.09) {
    summary.heightMm = sourceLayers * 0.05;
  }
  if (Layer_Height > 0.06) {
    summary.printLayers = sourceLayers / 2;
    summary.heightMm = 0.1 * summary.printLayers;
  }

  long exposureLayers = summary.printLayers - Base_Layer;
  if (exposureLayers < 0) exposureLayers = 0;
  long movementLayers = summary.printLayers - 1;
  if (movementLayers < 0) movementLayers = 0;
  summary.estimatedSecs = (Base_Layer * Base_Exposure) +
                          (exposureLayers * Regular_Exposure) +
                          (uint32_t)(motor_updown_time * movementLayers);
  return true;
}

bool importModelPngExists(const String &name, int index) {
  String path = "/" + name + "/" + String(index) + ".png";
  File entry = SD.open(path.c_str());
  bool exists = (bool)entry;
  if (entry) entry.close();
  return exists;
}

int countImportModelSourceLayers(const String &name) {
  int count = 0;
  while (count + 100 <= MAX_LAYER_FILES && importModelPngExists(name, count + 100)) count += 100;
  while (count + 1 <= MAX_LAYER_FILES && importModelPngExists(name, count + 1)) count++;
  return count;
}

bool scanZipModel(const char *zipPath, ModelSummary &summary) {
  char entry[256];
  UNZIP *zip = new UNZIP();
  if (!zip) return false;

  int minN = 0x7FFFFFFF, total = 0;
  if (zip->openZIP(zipPath, zipOpen, zipClose, zipRead, zipSeek) != UNZ_OK) {
    delete zip;
    return false;
  }
  zip->gotoFirstFile();
  do {
    zip->getFileInfo(NULL, entry, sizeof(entry), NULL, 0, NULL, 0);
    int n = layerIndexFromEntry(entry);
    if (n >= 0) {
      total++;
      if (n < minN) minN = n;
    }
  } while (zip->gotoNextFile() == UNZ_OK);
  zip->closeZIP();
  delete zip;

  if (!modelSummaryFromSourceLayers(total, summary)) return false;
  summary.sizeBytes = sdFileSize(String(zipPath));
  return true;
}

String uniqueModelName(const String &baseName) {
  String base = safeModelName(baseName);
  String candidate = base;
  for (int i = 2; i < 1000; i++) {
    bool isDir = false;
    if (!sdPathExists("/" + candidate, &isDir)) return candidate;
    candidate = base + "-" + String(i);
  }
  return base + "-" + String(millis());
}

String modelMetaJsonEscape(String s) {
  String out;
  out.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += c;
    }
  }
  return out;
}

bool writeModelMetadataFile(const String &destDir, const String &name,
                            const ModelSummary &summary,
                            const ModelImportOptions &options) {
  String path = destDir + "/model.json";
  SD.remove(path.c_str());
  File f = SD.open(path.c_str(), FILE_WRITE);
  if (!f) return false;

  String source = options.source.length() ? options.source : "unknown";
  f.print("{\n");
  f.print("  \"format_version\": 1,\n");
  f.print("  \"name\": \"");
  f.print(modelMetaJsonEscape(name));
  f.print("\",\n");
  f.print("  \"source\": \"");
  f.print(modelMetaJsonEscape(source));
  f.print("\",\n");
  f.print("  \"source_layers\": ");
  f.print(summary.sourceLayers);
  f.print(",\n");
  f.print("  \"layers\": ");
  f.print(summary.sourceLayers);
  f.print(",\n");
  f.print("  \"height_mm\": ");
  f.print(String(summary.heightMm, 2));
  f.print(",\n");
  f.print("  \"estimated_seconds\": ");
  f.print(summary.estimatedSecs);
  f.print(",\n");
  f.print("  \"archive_size_bytes\": ");
  f.print(summary.sizeBytes);
  if (options.resinKnown) {
    f.print(",\n  \"resin_ml\": ");
    f.print(String(options.resinMl, 2));
  }
  if (options.connectPublicId.length()) {
    f.print(",\n  \"connect_public_id\": \"");
    f.print(modelMetaJsonEscape(options.connectPublicId));
    f.print("\"");
  }
  if (options.connectUrl.length()) {
    f.print(",\n  \"connect_url\": \"");
    f.print(modelMetaJsonEscape(options.connectUrl));
    f.print("\"");
  }
  if (options.originalCredits.length()) {
    f.print(",\n  \"original_credits\": \"");
    f.print(modelMetaJsonEscape(options.originalCredits));
    f.print("\"");
  }
  if (options.licenseName.length()) {
    f.print(",\n  \"license\": \"");
    f.print(modelMetaJsonEscape(options.licenseName));
    f.print("\"");
  }
  f.print("\n}\n");
  f.close();
  return true;
}

bool replaceJsonBoolField(String &json, const char *key, bool value) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  int start = json.indexOf(needle);
  String repl = needle + (value ? "true" : "false");
  if (start < 0) {
    int end = json.lastIndexOf('}');
    if (end < 0) return false;
    json = json.substring(0, end) + ",\n  " + repl + "\n}";
    return true;
  }
  int valStart = start + needle.length();
  int valEnd = valStart;
  while (valEnd < (int)json.length() && isAlpha(json[valEnd])) valEnd++;
  json = json.substring(0, start) + repl + json.substring(valEnd);
  return true;
}

bool replaceJsonNumberField(String &json, const char *key, const String &value) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  int start = json.indexOf(needle);
  String repl = needle + " " + value;
  if (start < 0) {
    int end = json.lastIndexOf('}');
    if (end < 0) return false;
    json = json.substring(0, end) + ",\n  " + repl + "\n}";
    return true;
  }
  int valStart = start + needle.length();
  while (valStart < (int)json.length() && json[valStart] == ' ') valStart++;
  int valEnd = valStart;
  while (valEnd < (int)json.length() &&
         (isDigit(json[valEnd]) || json[valEnd] == '.' || json[valEnd] == '-')) {
    valEnd++;
  }
  json = json.substring(0, start) + repl + json.substring(valEnd);
  return true;
}

bool replaceJsonStringField(String &json, const char *key, const String &value) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  String repl = needle + " \"" + modelMetaJsonEscape(value) + "\"";
  int start = json.indexOf(needle);
  if (start < 0) {
    int end = json.lastIndexOf('}');
    if (end < 0) return false;
    json = json.substring(0, end) + ",\n  " + repl + "\n}";
    return true;
  }

  int valStart = start + needle.length();
  while (valStart < (int)json.length() && json[valStart] == ' ') valStart++;
  if (valStart >= (int)json.length() || json[valStart] != '"') return false;

  int valEnd = valStart + 1;
  bool escape = false;
  while (valEnd < (int)json.length()) {
    char c = json[valEnd++];
    if (escape) escape = false;
    else if (c == '\\') escape = true;
    else if (c == '"') break;
  }
  json = json.substring(0, start) + repl + json.substring(valEnd);
  return true;
}

bool readJsonNumberField(const String &json, const char *key, double &value) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  int start = json.indexOf(needle);
  if (start < 0) return false;

  int valStart = start + needle.length();
  while (valStart < (int)json.length() && json[valStart] == ' ') valStart++;
  int valEnd = valStart;
  while (valEnd < (int)json.length() &&
         (isDigit(json[valEnd]) || json[valEnd] == '.' || json[valEnd] == '-')) {
    valEnd++;
  }
  if (valEnd <= valStart) return false;
  value = json.substring(valStart, valEnd).toDouble();
  return value > 0;
}

bool readJsonStringField(const String &json, const char *key, String &value) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  int start = json.indexOf(needle);
  if (start < 0) return false;

  int valStart = start + needle.length();
  while (valStart < (int)json.length() && json[valStart] == ' ') valStart++;
  if (valStart >= (int)json.length() || json[valStart] != '"') return false;

  value = "";
  bool escape = false;
  for (int i = valStart + 1; i < (int)json.length(); i++) {
    char c = json[i];
    if (escape) {
      value += c;
      escape = false;
    } else if (c == '\\') {
      escape = true;
    } else if (c == '"') {
      return value.length() > 0;
    } else {
      value += c;
    }
  }
  return false;
}

bool readModelMetadataJson(const String &name, String &json) {
  String path = "/" + name + "/model.json";
  File f = SD.open(path.c_str());
  if (!f) return false;
  json = "";
  uint32_t sz = f.size();
  json.reserve((sz < 4096 ? sz : 4095) + 1);
  while (f.available() && json.length() < 4096) json += (char)f.read();
  f.close();
  return json.length() > 0 && json.length() < 4096;
}

bool writeModelMetadataJson(const String &name, const String &json) {
  String path = "/" + name + "/model.json";
  SD.remove(path.c_str());
  File out = SD.open(path.c_str(), FILE_WRITE);
  if (!out) return false;
  out.print(json);
  out.close();
  return true;
}

bool getModelMetadataSourceLayers(const String &name, int &layers) {
  String json;
  if (!readModelMetadataJson(name, json)) return false;
  double v = 0;
  if (!readJsonNumberField(json, "source_layers", v) || v < 1) return false;
  layers = (int)v;
  return true;
}

// Create model.json for a pre-metadata model so the next details open reads
// the layer count instead of re-scanning the folder (a 1000+ layer directory
// costs seconds per scan on SdFat).
void backfillModelMetadataLayers(const String &name, int sourceLayers) {
  if (sourceLayers < 1) return;
  String json;
  if (readModelMetadataJson(name, json)) return;  // already has metadata
  ModelSummary summary;
  if (!modelSummaryFromSourceLayers(sourceLayers, summary)) return;
  ModelImportOptions createOptions;
  createOptions.source = "unknown";
  writeModelMetadataFile("/" + name, name, summary, createOptions);
}

bool getModelMetadataResin(const String &name, double &resinMl) {
  String json;
  if (!readModelMetadataJson(name, json)) return false;
  return readJsonNumberField(json, "resin_ml", resinMl);
}

bool getModelMetadataConnectPublicId(const String &name, String &publicId) {
  String json;
  if (!readModelMetadataJson(name, json)) return false;
  return readJsonStringField(json, "connect_public_id", publicId);
}

bool setModelMetadataResin(const String &name, double resinMl) {
  String json;
  if (!readModelMetadataJson(name, json)) {
    ModelSummary summary;
    if (!modelSummaryFromSourceLayers(countImportModelSourceLayers(name), summary)) return false;
    ModelImportOptions createOptions;
    createOptions.source = "unknown";
    if (!writeModelMetadataFile("/" + name, name, summary, createOptions)) return false;
    if (!readModelMetadataJson(name, json)) return false;
  }

  if (!replaceJsonNumberField(json, "resin_ml", String(resinMl, 2))) return false;
  replaceJsonBoolField(json, "resin_estimated", true);
  return writeModelMetadataJson(name, json);
}

bool updateModelMetadataConnectShare(const String &name, const ModelImportOptions &options,
                                     const String &sharedModelName) {
  String json;
  if (!readModelMetadataJson(name, json)) {
    ModelSummary summary;
    if (!modelSummaryFromSourceLayers(countImportModelSourceLayers(name), summary)) return false;
    ModelImportOptions createOptions;
    createOptions.source = "unknown";
    if (!writeModelMetadataFile("/" + name, name, summary, createOptions)) return false;
    if (!readModelMetadataJson(name, json)) return false;
  }

  replaceJsonBoolField(json, "connect_shared", true);
  if (sharedModelName.length()) replaceJsonStringField(json, "connect_model_name", sharedModelName);
  if (options.connectPublicId.length()) replaceJsonStringField(json, "connect_public_id", options.connectPublicId);
  if (options.connectUrl.length()) replaceJsonStringField(json, "connect_url", options.connectUrl);
  if (options.originalCredits.length()) replaceJsonStringField(json, "original_credits", options.originalCredits);
  if (options.licenseName.length()) replaceJsonStringField(json, "license", options.licenseName);
  if (options.resinKnown) {
    replaceJsonNumberField(json, "resin_ml", String(options.resinMl, 2));
    replaceJsonBoolField(json, "resin_estimated", true);
  }
  return writeModelMetadataJson(name, json);
}

String tempModelPath(const char *prefix) {
  return String("/") + prefix + "_" + String((uint32_t)millis());
}

bool commitTempModel(const String &tempDir, const String &destDir, bool replace) {
  bool destIsDir = false;
  bool destExists = sdPathExists(destDir, &destIsDir);
  if (!destExists) return SD.rename(tempDir.c_str(), destDir.c_str());
  if (!replace || !destIsDir) return false;

  String backupDir = tempModelPath(".tm_backup");
  if (sdPathExists(backupDir)) deleteModelFolder(backupDir.c_str(), false);

  if (!SD.rename(destDir.c_str(), backupDir.c_str())) return false;
  if (SD.rename(tempDir.c_str(), destDir.c_str())) {
    deleteModelFolder(backupDir.c_str(), false);
    return true;
  }

  // Best-effort rollback: keep the old model available if the final rename
  // failed after moving it aside.
  SD.rename(backupDir.c_str(), destDir.c_str());
  return false;
}

bool unpackModelToEmptyDir(const char *zipPath, const char *destDir, ModelSummary &summary) {
  char entry[256];
  const int BUFSZ = 4096;

  if (!scanZipModel(zipPath, summary)) return false;

  // UNZIP object is ~40 KB -> allocate on heap only while unpacking.
  // Never make it global/static (overflows WROOM DRAM at link time).
  UNZIP *zip = new UNZIP();
  uint8_t *buf = (uint8_t *)malloc(BUFSZ);
  if (!zip || !buf) {
    if (zip) delete zip;
    if (buf) free(buf);
    return false;
  }

  SD.mkdir(destDir);

  bool ok = true;
  int done = 0;
  int minN = 0x7FFFFFFF;
  if (zip->openZIP(zipPath, zipOpen, zipClose, zipRead, zipSeek) != UNZ_OK) {
    delete zip; free(buf);
    return false;
  }
  zip->gotoFirstFile();
  do {
    zip->getFileInfo(NULL, entry, sizeof(entry), NULL, 0, NULL, 0);
    int n = layerIndexFromEntry(entry);
    if (n >= 0 && n < minN) minN = n;
  } while (zip->gotoNextFile() == UNZ_OK);
  zip->closeZIP();

  DBG("Unpack: %d layers (min index %d)\n", summary.sourceLayers, minN);

  if (zip->openZIP(zipPath, zipOpen, zipClose, zipRead, zipSeek) != UNZ_OK) {
    delete zip; free(buf);
    return false;
  }
  zip->gotoFirstFile();
  do {
    zip->getFileInfo(NULL, entry, sizeof(entry), NULL, 0, NULL, 0);
    int n = layerIndexFromEntry(entry);
    if (n < 0) continue;

    String outPath = String(destDir) + "/" + String(n - minN + 1) + ".png";
    SD.remove(outPath.c_str());
    File out = SD.open(outPath.c_str(), FILE_WRITE);
    if (!out) { ok = false; break; }

    if (zip->openCurrentFile() != UNZ_OK) { out.close(); ok = false; break; }
    int rc;
    while ((rc = zip->readCurrentFile(buf, BUFSZ)) > 0)
      out.write(buf, rc);
    zip->closeCurrentFile();
    out.close();
    if (rc < 0) { ok = false; break; }

    done++;
    if (done % 20 == 0 || done == summary.sourceLayers) {
      String p = String(done) + " / " + String(summary.sourceLayers);
      netMessage("Unpacking layers", p.c_str());
    }
  } while (zip->gotoNextFile() == UNZ_OK);
  zip->closeZIP();
  delete zip;
  free(buf);

  if (!ok || done != summary.sourceLayers) {
    deleteModelFolder(destDir, false);
    return false;
  }
  return true;
}

// ===================================================================================
// ZIP / SL1 unpacker
// Pass 1: find lowest layer number and count. Pass 2: extract each *.png
// as /<dest>/<n - min + 1>.png so the stock firmware (which probes 1.png,
// 2.png, ... with no gaps) sees a valid model regardless of source format.
// ===================================================================================
bool importZipModel(const char *zipPath, const String &requestedName,
                    const ModelImportOptions &options,
                    ModelImportResult &result, String &error) {
  String baseName = safeModelName(requestedName);
  String finalName = baseName;
  String destDir = "/" + finalName;
  bool destIsDir = false;
  bool exists = sdPathExists(destDir, &destIsDir);

  if (exists && !options.replace) {
    if (!options.autoRename) {
      error = "model exists";
      return false;
    }
    finalName = uniqueModelName(baseName);
    destDir = "/" + finalName;
    exists = false;
  }

  String tempDir = tempModelPath(".tm_unpack");
  if (sdPathExists(tempDir)) deleteModelFolder(tempDir.c_str(), false);

  ModelSummary summary;
  if (!unpackModelToEmptyDir(zipPath, tempDir.c_str(), summary)) {
    error = "unpack failed";
    return false;
  }

  if (!writeModelMetadataFile(tempDir, finalName, summary, options)) {
    deleteModelFolder(tempDir.c_str(), false);
    error = "metadata write failed";
    return false;
  }

  if (!commitTempModel(tempDir, destDir, exists && options.replace)) {
    deleteModelFolder(tempDir.c_str(), false);
    error = "model swap failed";
    return false;
  }

  result.finalName = finalName;
  result.renamed = finalName != baseName;
  result.summary = summary;
  return true;
}

// ===================================================================================
// On-device import: OK on a .sl1/.zip entry in the Print list
// ===================================================================================

/**
 * @brief Convert the selected archive from the SD root into a printable
 * model folder (same pipeline as a network upload). Deletes the archive on
 * success so files don't pile up; on failure the partial model folder is
 * already removed by the temp-dir import pipeline.
 */
void importSelectedArchive() {
  String src = "/" + String(foldersel_long);
  String name = safeModelName(String(foldersel_long));
  netMessage("Importing:", foldersel.c_str());
  ModelImportOptions options;
  options.autoRename = true;
  options.source = "sd_import";
  ModelImportResult result;
  String error;
  bool ok = importZipModel(src.c_str(), name, options, result, error);
  if (ok) {
    SD.remove(src.c_str());
    netMessage("Model ready:", result.finalName.c_str());
  } else {
    netMessage("Import FAILED", foldersel.c_str());
  }
  delay(1500);
}
