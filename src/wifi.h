#pragma once
#include <stdbool.h>
#include "settings.h"

typedef enum { WIFI_MODE_NONE, WIFI_MODE_STA, WIFI_MODE_AP } wifi_mode_t;

// Con WIFI_POLICY_AUTO intenta conectarse a la WiFi de casa y, si no puede,
// crea la red propia. Con WIFI_POLICY_AP_ONLY crea directamente la red propia.
// Llama a on_progress periódicamente mientras intenta conectar.
wifi_mode_t wifi_start(wifi_policy_t policy, void (*on_progress)(void));
const char *wifi_ip(void);
