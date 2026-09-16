#include "framework.h"
#include "shim/Arduino.h"
#include "shim/Preferences.h"
#include "shim/audio_hardware.h"
#include "../audio.h"
#include <algorithm>

static void resetAudio() {
  mockAudioReset();
  mockNvsReset();
}

TEST(audio, sleeping_boot_never_enables_amplifier_or_queues_jingle) {
  resetAudio();
  audioBegin(true);
  CHECK(audioEnabled());
  CHECK(!mockAmplifierWasEnabled());
  CHECK_EQ(mockAudioQueued(), (size_t)0);
  sfxPlay(SFX_HATCH);
  mockAudioRunQueued();
  CHECK(mockAudioSamples().empty());
  audioSetSleeping(false);
  CHECK(mockAmplifierOn());
  CHECK_EQ(mockAudioQueued(), (size_t)0); // el jingle no queda pendiente
}

TEST(audio, legacy_mute_survives_upgrade_and_startup) {
  resetAudio();
  Preferences prefs;
  prefs.begin("tamapoke", false);
  prefs.putBool("snd", false);
  audioBegin(false);
  CHECK_EQ(audioVolume(), AUDIO_OFF);
  CHECK(!mockAmplifierWasEnabled());
  CHECK_EQ(mockAudioQueued(), (size_t)0);
}

TEST(audio, legacy_on_keeps_original_volume_and_awake_jingle) {
  resetAudio();
  audioBegin(false);
  CHECK_EQ(audioVolume(), AUDIO_HIGH);
  CHECK(mockAmplifierOn());
  CHECK_EQ(mockAudioQueued(), (size_t)1);
  mockAudioRunQueued();
  CHECK(!mockAudioSamples().empty());
}

TEST(audio, all_four_volume_levels_survive_restart) {
  resetAudio();
  audioBegin(false);
  for (AudioVolume volume : {AUDIO_OFF, AUDIO_LOW, AUDIO_MEDIUM, AUDIO_HIGH}) {
    audioSetVolume(volume);
    mockAudioReset();
    audioBegin(false);
    CHECK_EQ(audioVolume(), volume);
    CHECK_EQ(mockAmplifierOn(), volume != AUDIO_OFF);
    Preferences prefs;
    prefs.begin("tamapoke", true);
    CHECK_EQ(prefs.getBool("snd"), volume != AUDIO_OFF);
  }
}

TEST(audio, low_medium_high_produce_distinct_pcm_levels) {
  resetAudio();
  audioBegin(false);
  mockAudioRunQueued();
  int previousPeak = 0;
  for (AudioVolume volume : {AUDIO_LOW, AUDIO_MEDIUM, AUDIO_HIGH}) {
    audioSetVolume(volume);
    mockAudioClearSamples();
    sfxPlay(SFX_TAP);
    mockAudioRunQueued();
    const auto &pcm = mockAudioSamples();
    CHECK_EQ(pcm.size(), (size_t)(16000 * 35 / 1000 * 2));
    int peak = 0;
    for (int16_t value : pcm) peak = std::max(peak, std::abs((int)value));
    CHECK(peak > previousPeak);
    CHECK(peak <= 5000); // HIGH no supera el nivel del firmware anterior
    previousPeak = peak;
    for (size_t i = 0; i < pcm.size(); i += 2) CHECK_EQ(pcm[i], pcm[i + 1]);
  }
}

TEST(audio, muting_or_sleeping_discards_queued_effects) {
  resetAudio();
  audioBegin(false);
  audioSetVolume(AUDIO_OFF);
  CHECK(!mockAmplifierOn());
  CHECK_EQ(mockAudioQueued(), (size_t)0);
  audioSetVolume(AUDIO_LOW);
  sfxPlay(SFX_EAT);
  CHECK_EQ(mockAudioQueued(), (size_t)1);
  audioSetSleeping(true);
  CHECK(!mockAmplifierOn());
  CHECK_EQ(mockAudioQueued(), (size_t)0);
  audioSetVolume(AUDIO_MEDIUM);
  CHECK(!mockAmplifierOn());
  audioSetSleeping(false);
  mockAudioRunQueued();
  CHECK(mockAudioSamples().empty());
}

TEST(audio, invalid_volume_is_bounded_and_missing_codec_stays_silent) {
  resetAudio();
  Preferences prefs;
  prefs.begin("tamapoke", false);
  prefs.putUChar("sndvol", 255);
  audioBegin(false);
  CHECK_EQ(audioVolume(), AUDIO_HIGH);
  audioSetVolume((AudioVolume)255);
  CHECK_EQ(audioVolume(), AUDIO_HIGH);
  mockAudioReset();
  mockCodecPresent(false);
  audioBegin(false);
  CHECK(!mockAmplifierWasEnabled());
  sfxPlay(SFX_TAP);
  CHECK_EQ(mockAudioQueued(), (size_t)0);
}
