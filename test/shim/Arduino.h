// Shim minimo de Arduino.h para ejecutar la logica del firmware en el host.
// Solo cubre lo que usan pet.cpp / i18n.cpp. El reloj es falso y controlable
// para que los tests sean deterministas.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- reloj falso -----------------------------------------------------------
uint32_t millis();
void mockSetMillis(uint32_t ms);
void mockAdvanceMillis(uint32_t ms);

// ---- random determinista (misma semantica que Arduino: [0, howbig) ) -------
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);
// fuerza el siguiente/los siguientes random() a un valor fijo (para tests)
void mockForceRandom(long value);   // todos los random() devuelven value
void mockClearForcedRandom();

// ---- Serial ----------------------------------------------------------------
struct MockSerial {
  int printf(const char *fmt, ...);
  void print(const char *s);
  void println(const char *s);
  void begin(unsigned long);
};
extern MockSerial Serial;
void mockSerialSilence(bool quiet);

// ---- utilidades ------------------------------------------------------------
template <class T> static inline T min(T a, T b) { return a < b ? a : b; }
template <class T> static inline T max(T a, T b) { return a > b ? a : b; }

// GPIO y tarea/cola usados por audio.cpp. El host ejecuta la tarea a demanda.
constexpr int OUTPUT = 1, HIGH = 1, LOW = 0;
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
using QueueHandle_t = void *;
constexpr int pdPASS = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
QueueHandle_t xQueueCreate(unsigned length, unsigned itemSize);
int xQueueSend(QueueHandle_t queue, const void *item, uint32_t wait);
int xQueueReceive(QueueHandle_t queue, void *item, uint32_t wait);
int xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
int xTaskCreatePinnedToCore(void (*task)(void *), const char *, unsigned, void *, unsigned, void *, int);
