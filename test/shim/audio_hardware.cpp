#include "Arduino.h"
#include "Wire.h"
#include "ESP_I2S.h"
#include "audio_hardware.h"
#include "SD_MMC.h"
#include "../../pin_config.h"
#include <deque>
#include <memory>
#include <algorithm>

TwoWire Wire;
static bool codecPresent = true, amplifierOn = false, amplifierWasEnabled = false;
static std::vector<int16_t> samples;
struct AudioQueue { unsigned capacity, itemSize; std::deque<std::vector<uint8_t>> items; };
static std::vector<std::unique_ptr<AudioQueue>> queues;
static AudioQueue *activeQueue = nullptr;
static void (*audioTask)(void *) = nullptr;
struct QueueEmpty {};
static void (*writeHook)() = nullptr;

void mockAudioReset() {
  queues.clear();
  activeQueue = nullptr;
  audioTask = nullptr;
  codecPresent = true;
  amplifierOn = amplifierWasEnabled = false;
  samples.clear();
  writeHook = nullptr;
  mockSdReset();
}
void mockAudioOnWrite(void (*hook)()) { writeHook = hook; }
void mockAudioRunQueued() {
  if (!audioTask) return;
  try { audioTask(nullptr); } catch (const QueueEmpty &) {}
}
void mockAudioClearSamples() { samples.clear(); }
const std::vector<int16_t> &mockAudioSamples() { return samples; }
bool mockAmplifierOn() { return amplifierOn; }
bool mockAmplifierWasEnabled() { return amplifierWasEnabled; }
size_t mockAudioQueued() { return activeQueue ? activeQueue->items.size() : 0; }
void mockCodecPresent(bool present) { codecPresent = present; }

void pinMode(int, int) {}
void digitalWrite(int pin, int value) {
  if (pin == PA) {
    amplifierOn = value == HIGH;
    amplifierWasEnabled = amplifierWasEnabled || amplifierOn;
  }
}
uint8_t TwoWire::endTransmission(bool) { return codecPresent ? 0 : 2; }
size_t I2SClass::write(uint8_t *data, size_t size) {
  const int16_t *pcm = reinterpret_cast<int16_t *>(data);
  samples.insert(samples.end(), pcm, pcm + size / sizeof(int16_t));
  if (writeHook) writeHook();
  return size;
}
QueueHandle_t xQueueCreate(unsigned length, unsigned itemSize) {
  queues.emplace_back(new AudioQueue{length, itemSize, {}});
  activeQueue = queues.back().get();
  return activeQueue;
}
int xQueueSend(QueueHandle_t handle, const void *item, uint32_t) {
  auto &queue = *static_cast<AudioQueue *>(handle);
  if (queue.items.size() == queue.capacity) return 0;
  const auto *bytes = static_cast<const uint8_t *>(item);
  queue.items.emplace_back(bytes, bytes + queue.itemSize);
  return pdPASS;
}
int xQueueReceive(QueueHandle_t handle, void *item, uint32_t) {
  auto &queue = *static_cast<AudioQueue *>(handle);
  if (queue.items.empty()) throw QueueEmpty{}; // bloqueada en la placa; cede al test
  memcpy(item, queue.items.front().data(), queue.itemSize);
  queue.items.pop_front();
  return pdPASS;
}
int xQueueReset(QueueHandle_t handle) {
  static_cast<AudioQueue *>(handle)->items.clear();
  return pdPASS;
}
void vQueueDelete(QueueHandle_t handle) {
  queues.erase(std::remove_if(queues.begin(), queues.end(),
    [handle](const auto &q) { return q.get() == handle; }), queues.end());
  if (activeQueue == handle) activeQueue = nullptr;
}
int xTaskCreatePinnedToCore(void (*task)(void *), const char *, unsigned, void *, unsigned, void *, int) {
  audioTask = task;
  return pdPASS;
}
