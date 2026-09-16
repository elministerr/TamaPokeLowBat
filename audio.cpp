#include "audio.h"
#include "cry.h"
#include "dex.h"
#include "pin_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include <Preferences.h>
#include <atomic>

// ---------------------------------------------------------------------------
// Audio del TamaPoke: códec ES8311 (DAC -> amplificador PA -> altavoz) por I2S.
// Init del ES8311 portado del driver oficial de Espressif (esp-bsp), fijado a
// MCLK=4.096MHz (256*fs), 16kHz, 16-bit, esclavo I2S. Los efectos son tonos
// cuadrados (estilo Game Boy) sintetizados en una tarea aparte para no
// bloquear el loop de juego.
// ---------------------------------------------------------------------------

#define ES8311_ADDR 0x18
#define SAMPLE_RATE 16000

static I2SClass i2s;
static std::atomic<bool> gReady{false};
static std::atomic<AudioVolume> gVolume{AUDIO_HIGH};
static std::atomic<bool> gSleeping{true};
static QueueHandle_t gQ = nullptr;
static std::atomic<bool> gCryPending{false};
static std::atomic<uint32_t> gGeneration{0};
enum AudioKind : uint8_t { EFFECT, CRY };
struct AudioEvent { uint32_t generation; uint16_t value; AudioKind kind; };

static bool canPlay(uint32_t generation) {
  return gReady && audioEnabled() && !gSleeping && generation == gGeneration;
}

static void cancelAudio() {
  ++gGeneration; // interrupts active playback even after a quick mute/unmute
  if (gQ) xQueueReset(gQ);
  gCryPending = false;
}

// El NS4150B tarda bastante mas de 8 ms en estabilizarse tras cada apagado, asi
// que encenderlo justo antes de cada efecto se comia los cortos: el jingle de
// arranque (440 ms) se oia y los pitidos de 35 ms no. Se deja encendido mientras
// haya sonido y la mascota este despierta, como en el ejemplo verificado de
// Waveshare, y solo se apaga al dormir o al silenciar.
static void updateAmplifierPower() {
  // gReady evita encenderlo en una placa donde el codec no arranco: ahi el
  // amplificador solo aportaria siseo, porque no va a sonar nada.
  digitalWrite(PA, (gReady && audioEnabled() && !gSleeping) ? HIGH : LOW);
}

// ---- I2C del códec ----
static bool esW(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static uint8_t esR(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ES8311_ADDR, 1);
  return Wire.available() ? Wire.read() : 0;
}

// Secuencia de init VERIFICADA EN ESTA PLACA (proyecto PlaneRadar2.0, misma
// Waveshare 1.75). Clave: reloj DERIVADO DEL BCLK (reg01=0xBF, sin MCLK externo)
// y referencia interna que alimenta el DAC (reg44=0x58); sin esos dos el codec
// respondia por I2C pero no salia audio. 16kHz, 16-bit, esclavo I2S.
static bool es8311Init() {
  Wire.beginTransmission(ES8311_ADDR);
  if (Wire.endTransmission() != 0) return false;

  // open()
  esW(0x0D, 0xFA); esW(0x44, 0x08); esW(0x44, 0x08);  // power up + quirk de 1a escritura
  esW(0x01, 0x30); esW(0x02, 0x00); esW(0x03, 0x10); esW(0x16, 0x24);
  esW(0x04, 0x10); esW(0x05, 0x00); esW(0x0B, 0x00); esW(0x0C, 0x00);
  esW(0x10, 0x1F); esW(0x11, 0x7F);
  esW(0x00, 0x80); esW(0x00, 0x80);                   // reset clock, esclavo
  esW(0x01, 0xBF);                                    // clk src = BCLK (sin MCLK externo)
  { uint8_t r = esR(0x06); r &= ~0x20; esW(0x06, r); }  // SCLK no invertido
  esW(0x13, 0x10); esW(0x1B, 0x0A); esW(0x1C, 0x6A);
  esW(0x44, 0x58);                                    // referencia interna -> alimenta el DAC

  // config_sample(): BCLK*8 = DIG_MCLK
  esW(0x02, 0x18); esW(0x05, 0x00); esW(0x03, 0x10); esW(0x04, 0x20);
  { uint8_t r = esR(0x07); r &= 0xC0; esW(0x07, r); }
  esW(0x08, 0xFF);
  { uint8_t r = esR(0x06); r &= 0xE0; r |= 0x03; esW(0x06, r); }  // bclk_div=4

  // formato I2S 16-bit
  esW(0x09, 0x0C); esW(0x0A, 0x0C);

  // start() DAC esclavo
  esW(0x00, 0x80); esW(0x01, 0xBF); esW(0x09, 0x0C); esW(0x0A, 0x0C);
  esW(0x17, 0xBF); esW(0x0E, 0x02); esW(0x12, 0x00); esW(0x14, 0x1A);
  esW(0x0D, 0x01); esW(0x15, 0x40); esW(0x37, 0x08); esW(0x45, 0x00);

  // volumen + unmute
  esW(0x32, 0xBF);                                    // volumen DAC ~0 dB
  { uint8_t r = esR(0x31); r &= 0x9F; esW(0x31, r); }  // unmute
  return true;
}

// ---- sintetizador de tono cuadrado ----
struct Note { uint16_t f, ms; };

static const Note N_TAP[]    = {{880, 35}};
static const Note N_EAT[]    = {{660, 45}, {0, 12}, {660, 45}};
static const Note N_PLAY[]   = {{784, 45}, {988, 60}};
static const Note N_HEART[]  = {{1047, 55}, {1319, 90}};
static const Note N_HATCH[]  = {{523, 80}, {659, 80}, {784, 110}, {1047, 170}};
static const Note N_EVOLVE[] = {{523, 80}, {659, 80}, {784, 80}, {1047, 90}, {1319, 230}};
static const Note N_MEDAL[]  = {{784, 70}, {0, 25}, {784, 70}, {0, 25}, {1047, 200}};
static const Note N_DENY[]   = {{300, 110}, {200, 170}};
static const Note N_BYE[]    = {{784, 150}, {659, 150}, {523, 280}};
static const Note N_LEVEL[]  = {{784, 70}, {1047, 130}};

struct SfxDef { const Note *n; uint8_t len; };
static const SfxDef SFX[SFX_COUNT] = {
  {N_TAP, 1}, {N_EAT, 3}, {N_PLAY, 2}, {N_HEART, 2}, {N_HATCH, 4},
  {N_EVOLVE, 5}, {N_MEDAL, 5}, {N_DENY, 2}, {N_BYE, 3}, {N_LEVEL, 2},
};

static int16_t buf[256 * 2];  // estéreo intercalado (L=R)
static const int16_t amplitudes[AUDIO_VOLUME_COUNT] = {0, 625, 1250, 5000};

// reproduce un tono (o silencio si f==0) con rampa de ataque/caida anti-click
static void playTone(uint16_t f, uint16_t ms, uint32_t generation) {
  int total = SAMPLE_RATE * ms / 1000;
  int half = f ? (SAMPLE_RATE / (2 * f)) : 0;  // medio periodo en muestras
  int phase = 0, done = 0;
  bool high = true;
  while (done < total) {
    if (!canPlay(generation)) return;
    const int16_t amp = amplitudes[audioVolume()];
    int n = total - done; if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
      int16_t s = 0;
      if (f) {
        s = high ? amp : -amp;
        int idx = done + i;
        if (idx < 64) s = (int16_t)(s * idx / 64);                 // ataque
        else if (idx > total - 96) s = (int16_t)(s * (total - idx) / 96);  // caida
        if (++phase >= half) { phase = 0; high = !high; }
      }
      buf[i * 2] = s; buf[i * 2 + 1] = s;
    }
    i2s.write((uint8_t *)buf, n * 4);
    done += n;
  }
}

static bool playCry(int16_t dex, uint32_t generation) {
  CryFile cry;
  if (!cry.open(dex)) {
    Serial.printf("cry #%d: missing/invalid WAV; using affection tone\n", dex);
    return false;
  }
  Serial.printf("cry=%s samples=%lu\n", cry.path(), (unsigned long)cry.sampleCount());
  int16_t mono[256];
  uint32_t done = 0;
  while (canPlay(generation)) {
    size_t n = cry.read(mono, 256);
    if (!n) break;
    int16_t amp = amplitudes[audioVolume()];
    for (size_t i = 0; i < n; ++i) {
      int32_t s = (int32_t)mono[i] * amp / 32768;
      uint32_t index = done + i, left = cry.sampleCount() - index;
      if (index < 64) s = s * (int32_t)index / 64;
      if (left < 96) s = s * (int32_t)left / 96;
      buf[i * 2] = buf[i * 2 + 1] = (int16_t)s;
    }
    if (!canPlay(generation)) break;
    i2s.write((uint8_t *)buf, n * 4);
    done += n;
  }
  return true;
}

static void audioTask(void *) {
  AudioEvent event;
  for (;;) {
    if (!xQueueReceive(gQ, &event, portMAX_DELAY)) continue;
    if (canPlay(event.generation)) {
      uint16_t id = event.value;
      bool effect = event.kind == EFFECT;
      if (event.kind == CRY && !playCry(id, event.generation)) {
        id = SFX_HEART;
        effect = true;
      }
      if (effect && id < SFX_COUNT) {
        const SfxDef &d = SFX[id];
        for (uint8_t i = 0; i < d.len && canPlay(event.generation); i++)
          playTone(d.n[i].f, d.n[i].ms, event.generation);
      }
    }
    if (event.kind == CRY && event.generation == gGeneration) gCryPending = false;
  }
}

void audioBegin(bool sleeping) {
  gReady = false;
  gSleeping = sleeping;
  gQ = nullptr;
  gCryPending = false;
  gGeneration = 0;
  // I2S primero: arranca el MCLK que necesita el códec para engancharse
  pinMode(PA, OUTPUT);
  digitalWrite(PA, LOW);   // hasta saber si el sonido esta activado

  Preferences p;
  p.begin("tamapoke", true);
  // Migracion: OFF sigue apagado; el antiguo ON conserva el volumen original.
  uint8_t volume = p.getUChar("sndvol", p.getBool("snd", true) ? AUDIO_HIGH : AUDIO_OFF);
  gVolume = volume < AUDIO_VOLUME_COUNT ? (AudioVolume)volume : AUDIO_HIGH;
  p.end();

  i2s.setPins(I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO, I2S_DI_IO, I2S_MCK_IO);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("I2S begin fallo");
    return;
  }
  if (!es8311Init()) { Serial.println("ES8311 no responde (audio off)"); return; }

  gQ = xQueueCreate(8, sizeof(AudioEvent));
  if (!gQ) { Serial.println("Sin memoria para audio"); return; }
  if (xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 1, nullptr, 0) != pdPASS) {
    vQueueDelete(gQ);
    gQ = nullptr;
    Serial.println("No se pudo iniciar audio");
    return;
  }
  gReady = true;
  updateAmplifierPower();
  sfxPlay(SFX_HATCH);  // solo suena si la mascota arranca despierta y hay volumen
}

void sfxPlay(uint8_t id) {
  AudioEvent event{gGeneration.load(), id, EFFECT};
  if (gQ && id < SFX_COUNT && canPlay(event.generation)) xQueueSend(gQ, &event, 0);
}

bool cryPlay(int16_t dex) {
  AudioEvent event{gGeneration.load(), (uint16_t)dex, CRY};
  if (!gQ || dex < 1 || dex > DEX_COUNT || !canPlay(event.generation) ||
      gCryPending.exchange(true)) return false;
  if (xQueueSend(gQ, &event, 0) == pdPASS) return true;
  gCryPending = false;
  return false;
}

void audioSetVolume(AudioVolume volume) {
  if (volume >= AUDIO_VOLUME_COUNT || volume == audioVolume()) return;
  gVolume = volume;
  if (volume == AUDIO_OFF) cancelAudio();
  updateAmplifierPower();
  Preferences p;
  p.begin("tamapoke", false);
  p.putUChar("sndvol", volume);
  p.putBool("snd", volume != AUDIO_OFF);  // compatible si se vuelve al firmware anterior
  p.end();
}
AudioVolume audioVolume() { return gVolume.load(); }
bool audioEnabled() { return audioVolume() != AUDIO_OFF; }

void audioSetSleeping(bool sleeping) {
  if (gSleeping == sleeping) return;
  gSleeping = sleeping;
  if (sleeping) cancelAudio();
  updateAmplifierPower();  // durmiendo no hay efectos: amp apagado, sin consumo
}
