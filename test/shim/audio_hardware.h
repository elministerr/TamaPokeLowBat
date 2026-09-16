#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>

void mockAudioReset();
void mockAudioRunQueued();
void mockAudioClearSamples();
const std::vector<int16_t> &mockAudioSamples();
bool mockAmplifierOn();
bool mockAmplifierWasEnabled();
size_t mockAudioQueued();
void mockCodecPresent(bool present);
void mockAudioOnWrite(void (*hook)());
