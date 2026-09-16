#pragma once
#include <stdint.h>

// Efectos de sonido del juego (cola, no bloqueante). El orden coincide con la
// tabla SFX de audio.cpp.
enum Sfx : uint8_t {
  SFX_TAP = 0,  // tocar / boton
  SFX_EAT,      // comer
  SFX_PLAY,     // punto del minijuego / golpe
  SFX_HEART,    // le gusta / mimo
  SFX_HATCH,    // eclosion
  SFX_EVOLVE,   // evolucion
  SFX_MEDAL,    // medalla / hito
  SFX_DENY,     // accion no permitida
  SFX_BYE,      // despedida
  SFX_LEVEL,    // sube de nivel
  SFX_COUNT
};

enum AudioVolume : uint8_t { AUDIO_OFF = 0, AUDIO_LOW, AUDIO_MEDIUM, AUDIO_HIGH, AUDIO_VOLUME_COUNT };

void audioBegin(bool sleeping);  // aplicar el sueno ANTES de encender el amplificador
void sfxPlay(uint8_t id);   // encola un efecto (no bloquea el loop)
void audioSetVolume(AudioVolume volume);
AudioVolume audioVolume();
bool audioEnabled();
void audioSetSleeping(bool sleeping);  // dormida: amplificador apagado
