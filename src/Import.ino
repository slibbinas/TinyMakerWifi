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

// ===================================================================================
// ZIP / SL1 unpacker
// Pass 1: find lowest layer number and count. Pass 2: extract each *.png
// as /<dest>/<n - min + 1>.png so the stock firmware (which probes 1.png,
// 2.png, ... with no gaps) sees a valid model regardless of source format.
// ===================================================================================
bool unpackModel(const char *zipPath, const char *destDir) {
  char entry[256];
  const int BUFSZ = 4096;

  // UNZIP object is ~40 KB -> allocate on heap only while unpacking.
  // Never make it global/static (overflows WROOM DRAM at link time).
  UNZIP *zip = new UNZIP();
  uint8_t *buf = (uint8_t *)malloc(BUFSZ);
  if (!zip || !buf) {
    if (zip) delete zip;
    if (buf) free(buf);
    return false;
  }

  // ---- Pass 1: scan - find lowest layer number and count
  int minN = 0x7FFFFFFF, total = 0;
  if (zip->openZIP(zipPath, zipOpen, zipClose, zipRead, zipSeek) != UNZ_OK) {
    delete zip; free(buf);
    return false;
  }
  zip->gotoFirstFile();
  do {
    zip->getFileInfo(NULL, entry, sizeof(entry), NULL, 0, NULL, 0);
    int n = layerIndexFromEntry(entry);
    if (n >= 0) { total++; if (n < minN) minN = n; }
  } while (zip->gotoNextFile() == UNZ_OK);
  zip->closeZIP();
  if (total == 0) { delete zip; free(buf); return false; }
  DBG("Unpack: %d layers (min index %d)\n", total, minN);

  // ---- Prepare destination
  SD.mkdir(destDir);

  // Remove stale layers from a previous upload with the same model name.
  // Leftover files above the new layer count would inflate the count seen
  // by the firmware's contiguous-file probing (mixed/oversized model!).
  for (int i = 1; ; i++) {
    String p = String(destDir) + "/" + String(i) + ".png";
    if (!SD.remove(p.c_str())) break;
  }

  // ---- Pass 2: extract each *.png as <n - minN + 1>.png
  bool ok = true;
  int done = 0;
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
    if (done % 20 == 0 || done == total) {
      String p = String(done) + " / " + String(total);
      netMessage("Unpacking layers", p.c_str());
    }
  } while (zip->gotoNextFile() == UNZ_OK);
  zip->closeZIP();
  delete zip;
  free(buf);

  // On failure remove the partial folder - a model with missing layers
  // would otherwise print incomplete (firmware probes until first gap).
  if (!ok) {
    for (int i = 1; i <= total; i++) {
      String p = String(destDir) + "/" + String(i) + ".png";
      SD.remove(p.c_str());
    }
    SD.rmdir(destDir);
  }
  return ok;
}

// ===================================================================================
// On-device import: OK on a .sl1/.zip entry in the Print list
// ===================================================================================

/**
 * @brief Convert the selected archive from the SD root into a printable
 * model folder (same pipeline as a network upload). Deletes the archive on
 * success so files don't pile up; on failure the partial model folder is
 * already removed by unpackModel().
 */
void importSelectedArchive() {
  String src = "/" + String(foldersel_long);
  String name = safeModelName(String(foldersel_long));
  String dest = "/" + name;
  netMessage("Importing:", foldersel.c_str());
  bool ok = unpackModel(src.c_str(), dest.c_str());
  if (ok) {
    SD.remove(src.c_str());
    netMessage("Model ready:", name.c_str());
  } else {
    netMessage("Import FAILED", foldersel.c_str());
  }
  delay(1500);
}
