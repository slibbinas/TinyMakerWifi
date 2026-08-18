#pragma once

#include <Arduino.h>

struct ModelSummary {
  int sourceLayers = 0;
  int printLayers = 0;
  float heightMm = 0;
  uint32_t estimatedSecs = 0;
  uint32_t sizeBytes = 0;        // the ARCHIVE (.zip/.sl1) it came from
  uint32_t folderBytes = 0;      // the unpacked layers on the card; 0 = unknown
                                 // (summed while unpacking - walking the folder
                                 // later would be O(models x layers))
  float slicedLayerHeightMm = 0;  // from the archive's config.ini; 0 = unknown
};

struct ModelImportOptions {
  bool replace = false;
  bool autoRename = false;
  String source = "unknown";
  String connectPublicId = "";
  String connectUrl = "";
  String originalCredits = "";
  String licenseName = "";
  bool resinKnown = false;
  double resinMl = 0.0;
  // Arrival order, so the dashboard can put the newest model on top. A counter
  // and not a clock: a printer with no internet never syncs NTP, and sorting by
  // a date that is always zero would silently fall back to A-Z for exactly the
  // people who cannot tell why (V, 08-19).
  uint32_t importSeq = 0;
  uint32_t createdEpoch = 0;     // extra, for display only; 0 = clock never synced
};

struct ModelImportResult {
  String finalName = "";
  bool renamed = false;
  ModelSummary summary;
};
