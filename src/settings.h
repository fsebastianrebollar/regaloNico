#pragma once
#include <stdbool.h>
#include <stdint.h>

// Ajustes persistentes (último sector de la flash). Se trabaja sobre una copia
// en RAM; settings_save_if_dirty() la escribe desde el bucle principal.

#define WIFI_SSID_MAX 32
#define WIFI_PASS_MAX 63

#define NUM_PRESETS 5

typedef struct {
    uint8_t used;
    uint8_t r, g, b;
    uint8_t fx;  // led_effect_t
    uint8_t reserved[3];
} led_preset_t;

void settings_init(void);
bool settings_save_if_dirty(void);  // true si se ha escrito (o no hacía falta)

// WiFi externa configurada desde la web. SSID vacío = red propia.
const char *settings_wifi_ssid(void);
const char *settings_wifi_pass(void);
void settings_set_wifi(const char *ssid, const char *pass);

const led_preset_t *settings_preset(int i);           // NULL si está vacío
void settings_set_preset(int i, const led_preset_t *p);  // NULL lo borra
