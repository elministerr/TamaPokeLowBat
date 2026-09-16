#include "SD_MMC.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
MockSD SD_MMC;
static std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
File MockSD::open(const char *path, const char *) {
  auto it = files.find(path);
  return it == files.end() ? File() : File(it->second);
}
void mockSdFile(const char *path, const std::vector<uint8_t> &data) {
  files[path] = std::make_shared<std::vector<uint8_t>>(data);
}
void mockSdReset() { files.clear(); }
size_t File::read(uint8_t *dest, size_t bytes) {
  if (!data) return 0;
  bytes = std::min(bytes, size() - offset);
  if (bytes) std::memcpy(dest, data->data() + offset, bytes);
  offset += bytes;
  return bytes;
}
