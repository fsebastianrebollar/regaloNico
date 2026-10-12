#pragma once
#include <stdint.h>

// Mantener los dos botones HOLD_MS borra la WiFi configurada (vuelve a la
// red propia), lo guarda en flash y reinicia.
void mode_switch_tick(void);

// Reinicio diferido (p. ej. tras cambiar la WiFi desde la web, para que dé
// tiempo a enviar la respuesta). Lo ejecuta mode_switch_tick().
void mode_switch_reboot_in(uint32_t ms);
