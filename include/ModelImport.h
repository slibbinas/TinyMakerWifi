#pragma once

#include <Arduino.h>

struct ModelSummary {
  int sourceLayers = 0;
  int printLayers = 0;
  float heightMm = 0;
  uint32_t estimatedSecs = 0;
  uint32_t sizeBytes = 0;
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
};

struct ModelImportResult {
  String finalName = "";
  bool renamed = false;
  ModelSummary summary;
};
