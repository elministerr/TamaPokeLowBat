#include "framework.h"
#include "shim/SD_MMC.h"
#include "shim/audio_hardware.h"
#include "shim/Preferences.h"
#include "../audio.h"
#include "../cry.h"
#include <algorithm>

static void put32(std::vector<uint8_t> &v, size_t at, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) v[at + i] = n >> (8 * i);
}
static std::vector<uint8_t> wav(int16_t sample = -32768, size_t frames = 800, bool metadata = false) {
  std::vector<uint8_t> v = {
    'R','I','F','F',0,0,0,0,'W','A','V','E',
    'f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0x3e,0,0,
    0,0x7d,0,0,2,0,16,0
  };
  if (metadata) {
    const uint8_t junk[] = {'J','U','N','K',3,0,0,0,1,2,3,0};
    v.insert(v.end(), junk, junk + sizeof(junk));
  }
  const uint8_t data[] = {'d','a','t','a',0,0,0,0};
  v.insert(v.end(), data, data + sizeof(data));
  put32(v, v.size() - 4, frames * 2);
  for (size_t i = 0; i < frames; ++i) {
    v.push_back((uint16_t)sample & 255);
    v.push_back((uint16_t)sample >> 8);
  }
  put32(v, 4, v.size() - 8);
  return v;
}
static void startAudio() {
  mockAudioReset(); mockNvsReset();
  audioBegin(false); mockAudioRunQueued(); mockAudioClearSamples();
}

TEST(cry, reads_pcm_and_skips_odd_length_metadata) {
  mockSdReset();
  mockSdFile("/cries/123.wav", wav(-12345, 300, true));
  CryFile c;
  CHECK(c.open(123)); CHECK_EQ(c.sampleCount(), (uint32_t)300);
  int16_t pcm[256];
  CHECK_EQ(c.read(pcm, 256), (size_t)256);
  for (int16_t s : pcm) CHECK_EQ(s, (int16_t)-12345);
  CHECK_EQ(c.read(pcm, 256), (size_t)44);
  CHECK_EQ(c.read(pcm, 256), (size_t)0);
}

TEST(cry, accepts_unpadded_filenames_and_rejects_invalid_species) {
  mockSdReset(); mockSdFile("/cries/4.wav", wav());
  CryFile c;
  CHECK(c.open(4)); CHECK(std::string(c.path()) == "/cries/4.wav");
  CHECK(!c.open(0)); CHECK_EQ(c.sampleCount(), (uint32_t)0);
  CHECK(!c.open(-1)); CHECK(!c.open(152)); CHECK(!c.open(25));
}

TEST(cry, rejects_bad_headers_formats_and_truncated_data) {
  mockSdReset();
  auto valid = wav();
  // Signature, PCM codec, channels, sample rate, byte rate, alignment, bit depth.
  for (size_t field : {0, 8, 20, 22, 24, 28, 32, 34}) {
    auto bad = valid; bad[field] ^= 2;
    mockSdFile("/cries/001.wav", bad);
    CryFile c; CHECK(!c.open(1));
  }
  for (size_t length : {0, 11, 20, 43, 100}) {
    auto bad = valid; bad.resize(length);
    mockSdFile("/cries/001.wav", bad);
    CryFile c; CHECK(!c.open(1));
  }
  for (size_t field : {4, 16, 40}) {
    auto bad = valid; put32(bad, field, UINT32_MAX);
    mockSdFile("/cries/001.wav", bad);
    CryFile c; CHECK(!c.open(1));
  }
  for (uint32_t bytes : {0u, 1599u}) {
    auto bad = valid; put32(bad, 40, bytes);
    mockSdFile("/cries/001.wav", bad);
    CryFile c; CHECK(!c.open(1));
  }
  mockSdFile("/cries/001.wav", wav(1, 160001));
  CryFile c; CHECK(!c.open(1));
}

TEST(cry_audio, selects_displayed_species_and_scales_all_volume_levels) {
  startAudio();
  mockSdFile("/cries/123.wav", wav());
  mockSdFile("/cries/025.wav", wav(16384));
  for (AudioVolume level : {AUDIO_LOW, AUDIO_MEDIUM, AUDIO_HIGH}) {
    audioSetVolume(level); mockAudioClearSamples();
    CHECK(cryPlay(123)); mockAudioRunQueued();
    const auto pcm = mockAudioSamples();
    CHECK_EQ(pcm.size(), (size_t)1600);
    int peak = 0;
    for (int16_t s : pcm) { CHECK(s <= 0); peak = std::max(peak, -(int)s); }
    CHECK_EQ(peak, level == AUDIO_LOW ? 625 : level == AUDIO_MEDIUM ? 1250 : 5000);
    for (size_t i = 0; i < pcm.size(); i += 2) CHECK_EQ(pcm[i], pcm[i + 1]);
  }
  mockAudioClearSamples(); CHECK(cryPlay(25)); mockAudioRunQueued();
  CHECK_EQ(mockAudioSamples()[200], (int16_t)2500);
}

TEST(cry_audio, silence_sleep_and_invalid_species_do_not_queue_cries) {
  startAudio();
  CHECK(!cryPlay(0)); CHECK(!cryPlay(-1)); CHECK(!cryPlay(152));
  audioSetVolume(AUDIO_OFF); CHECK(!cryPlay(123));
  audioSetVolume(AUDIO_LOW); audioSetSleeping(true); CHECK(!cryPlay(123));
  CHECK_EQ(mockAudioQueued(), (size_t)0);
  CHECK(mockAudioSamples().empty()); CHECK(!mockAmplifierOn());
}

TEST(cry_audio, repeated_taps_do_not_accumulate_and_full_queue_recovers) {
  startAudio(); mockSdFile("/cries/123.wav", wav());
  CHECK(cryPlay(123));
  for (int i = 0; i < 20; ++i) CHECK(!cryPlay(123));
  CHECK_EQ(mockAudioQueued(), (size_t)1);
  mockAudioRunQueued(); CHECK_EQ(mockAudioSamples().size(), (size_t)1600);
  for (int i = 0; i < 8; ++i) sfxPlay(SFX_TAP);
  CHECK(!cryPlay(123)); mockAudioRunQueued();
  CHECK(cryPlay(123)); mockAudioRunQueued();
}

TEST(cry_audio, missing_or_invalid_recording_uses_affection_tone) {
  startAudio(); sfxPlay(SFX_HEART); mockAudioRunQueued();
  auto expected = mockAudioSamples(); mockAudioClearSamples();
  CHECK(cryPlay(123)); mockAudioRunQueued(); CHECK(mockAudioSamples() == expected);
  mockSdFile("/cries/123.wav", {1, 2, 3}); mockAudioClearSamples();
  CHECK(cryPlay(123)); mockAudioRunQueued(); CHECK(mockAudioSamples() == expected);
}

TEST(cry_audio, sleep_or_mute_interrupts_active_cry_even_if_immediately_reversed) {
  for (bool sleep : {false, true}) {
    startAudio(); mockSdFile("/cries/123.wav", wav());
    mockAudioOnWrite(sleep ? +[]() { audioSetSleeping(true); audioSetSleeping(false); }
                          : +[]() { audioSetVolume(AUDIO_OFF); audioSetVolume(AUDIO_HIGH); });
    CHECK(cryPlay(123)); mockAudioRunQueued();
    CHECK_EQ(mockAudioSamples().size(), (size_t)512); // one chunk, then canceled
    mockAudioOnWrite(nullptr); mockAudioClearSamples();
    CHECK(cryPlay(123)); mockAudioRunQueued();
    CHECK_EQ(mockAudioSamples().size(), (size_t)1600);
  }
}

TEST(cry_audio, queued_cry_is_discarded_on_sleep_and_volume_changes_live) {
  startAudio(); mockSdFile("/cries/123.wav", wav());
  CHECK(cryPlay(123)); audioSetSleeping(true); audioSetSleeping(false);
  mockAudioRunQueued(); CHECK(mockAudioSamples().empty());
  mockAudioOnWrite(+[]() { audioSetVolume(AUDIO_LOW); });
  CHECK(cryPlay(123)); mockAudioRunQueued();
  CHECK_EQ(mockAudioSamples()[200], (int16_t)-5000);
  CHECK_EQ(mockAudioSamples()[600], (int16_t)-625);
}
