#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "settings.h"

// Estado que la web modifica y el bucle principal aplica
typedef enum { FX_OFF, FX_SOLID, FX_RAINBOW, FX_CHASE, FX_BREATHE, FX_HEARTBEAT, FX_DISCO, FX_SPAIN, FX_COUNT } led_effect_t;

typedef struct {
    uint8_t r, g, b;
    uint8_t brightness;    // 0..255
    led_effect_t effect;
    int motor;             // -100..100 (objetivo; el bucle hace rampa)
    int preset;            // modo de LED guardado activo, -1 si se ha tocado a mano
    int motor_step;        // peldaño de la escalera del botón 2
} control_state_t;

extern volatile control_state_t control;

const char *control_effect_name(led_effect_t fx);
int control_effect_from_name(const char *name);  // -1 si no existe
void control_tick(void);

// Tras los modos guardados hay un sexto modo fijo "apagado" (no sale en la web)
#define PRESET_OFF NUM_PRESETS

bool control_apply_preset(int i);    // false si el hueco está vacío
void control_next_preset(void);      // botón 1: modos guardados y luego apagado
void control_start_preset(void);     // al arrancar: modo 1 (o el primero guardado)
void control_motor_stair(void);      // botón 2

// Fiestas temporales: durante ms cambian luces y motor y luego todo vuelve a
// como estaba. Las controla la propia Pico (no dependen de la web).
typedef enum { PARTY_NONE, PARTY_CRAZY, PARTY_GOL } party_t;
void control_crazy_start(uint32_t ms);  // modo loco: discoteca a tope y motor al 100 %
void control_gol_start(uint32_t ms);    // ¡gol!: bandera de España y la copa girando
uint32_t control_party_left_ms(party_t kind);  // 0 si no está activa
uint32_t control_crazy_left_ms(void);
void control_party_stop(party_t kind);  // la termina ya (si es esa) y restaura

// Tarjeta roja: cancela el modo loco, para el motor y apaga las luces
void control_stop_all(void);

// Capa temporal que tapa el efecto: llena la tira hasta level (0..255) con un color
void control_overlay(uint8_t level, uint8_t r, uint8_t g, uint8_t b);
void control_overlay_off(void);  // llamar a menudo desde el bucle principal
