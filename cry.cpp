#include "cry.h"
#include "dex.h"
#include <SD_MMC.h>
#include <stdio.h>
#include <string.h>

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) {
  return le16(p) | (uint32_t)le16(p + 2) << 16;
}

bool CryFile::open(int16_t dex) {
  file.close();
  remaining = totalSamples = 0;
  if (dex < 1 || dex > DEX_COUNT) return false;
  snprintf(filePath, sizeof(filePath), "/cries/%03d.wav", dex);
  file = SD_MMC.open(filePath, FILE_READ);
  if (!file && dex < 100) { // also accept the unpadded PokéAPI species number
    snprintf(filePath, sizeof(filePath), "/cries/%d.wav", dex);
    file = SD_MMC.open(filePath, FILE_READ);
  }
  if (file && parse()) return true;
  file.close();
  return false;
}

bool CryFile::parse() {
  uint8_t header[16];
  if (file.size() < 44 || file.size() > 1024 * 1024 ||
      file.read(header, 12) != 12 || memcmp(header, "RIFF", 4) ||
      memcmp(header + 8, "WAVE", 4)) return false;
  uint32_t riffSize = le32(header + 4);
  if (riffSize < 36 || riffSize > file.size() - 8) return false;
  uint32_t end = riffSize + 8;
  bool haveFormat = false;
  while (file.position() <= end && end - file.position() >= 8) {
    if (file.read(header, 8) != 8) return false;
    uint32_t bytes = le32(header + 4), start = file.position();
    if (bytes > end - start) return false;
    if (!memcmp(header, "fmt ", 4)) {
      if (bytes < 16 || file.read(header, 16) != 16 ||
          le16(header) != 1 || le16(header + 2) != 1 ||
          le32(header + 4) != 16000 || le32(header + 8) != 32000 ||
          le16(header + 12) != 2 || le16(header + 14) != 16) return false;
      haveFormat = true;
    } else if (!memcmp(header, "data", 4)) {
      if (!haveFormat || bytes == 0 || (bytes & 1) || bytes > 16000 * 2 * 10)
        return false; // reject malformed files and cries longer than 10 seconds
      remaining = bytes;
      totalSamples = bytes / 2;
      return true;
    }
    // RIFF chunks, including unknown metadata, are padded to even lengths.
    uint32_t padded = bytes + (bytes & 1);
    if (padded > end - start || !file.seek(start + padded)) return false;
  }
  return false;
}

size_t CryFile::read(int16_t *samples, size_t count) {
  if (count > remaining / 2) count = remaining / 2;
  size_t bytes = file.read(reinterpret_cast<uint8_t *>(samples), count * 2);
  remaining = bytes == count * 2 ? remaining - bytes : 0;
  // ESP32 and the host test targets are little-endian, matching PCM WAV.
  return bytes / 2;
}
