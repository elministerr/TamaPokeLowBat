#pragma once
#include <FS.h>

// Small streaming reader for /cries/NNN.wav: PCM, mono, 16-bit, 16 kHz.
// No allocation proportional to the file size, and no decoder on the UI task.
class CryFile {
 public:
  bool open(int16_t dex);
  size_t read(int16_t *samples, size_t count);
  uint32_t sampleCount() const { return totalSamples; }
  const char *path() const { return filePath; }

 private:
  bool parse();
  File file;
  char filePath[24] = {};
  uint32_t remaining = 0, totalSamples = 0;
};
