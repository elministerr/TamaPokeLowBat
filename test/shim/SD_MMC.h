#pragma once
#include "FS.h"
constexpr const char *FILE_READ = "r";
struct MockSD {
  File open(const char *path, const char *mode = FILE_READ);
};
extern MockSD SD_MMC;
void mockSdFile(const char *path, const std::vector<uint8_t> &data);
void mockSdReset();
