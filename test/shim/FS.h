#pragma once
#include <stdint.h>
#include <stddef.h>
#include <memory>
#include <vector>

class File {
 public:
  File() = default;
  explicit File(std::shared_ptr<std::vector<uint8_t>> bytes) : data(bytes) {}
  explicit operator bool() const { return (bool)data; }
  size_t size() const { return data ? data->size() : 0; }
  size_t position() const { return offset; }
  bool seek(uint32_t pos) { if (pos > size()) return false; offset = pos; return true; }
  size_t read(uint8_t *dest, size_t bytes);
  void close() { data.reset(); offset = 0; }
 private:
  std::shared_ptr<std::vector<uint8_t>> data;
  size_t offset = 0;
};
