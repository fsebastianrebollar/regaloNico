#pragma once
#include <stdbool.h>
#include <stdint.h>

// Ajustes persistentes (último sector de la flash). Se trabaja sobre una copia
// en RAM; settings_save_if_dirty() la escribe desde el bucle principal.

typedef enum {
    WIFI_POLICY_AUTO = 0,     // SEABASS si está; si no, red propia
    WIFI_POLICY_AP_ONLY = 1,  // siempre red propia
} wifi_policy_t;

#define NUM_PRESETS 5

typedef struct {
    uint8_t used;
    uint8_t r, g, b;
    uint8_t fx;  // led_effect_t
    uint8_t reserved[3];
} led_preset_t;

void settings_init(void);
bool settings_save_if_dirty(void);  // true si se ha escrito (o no hacía falta)

wifi_policy_t settings_wifi_policy(void);
void settings_set_wifi_policy(wifi_policy_t p);

const led_preset_t *settings_preset(int i);           // NULL si está vacío
void settings_set_preset(int i, const led_preset_t *p);  // NULL lo borra
