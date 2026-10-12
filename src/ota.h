#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Actualizaciones del firmware. La flash tiene dos particiones (A/B): la nueva
// versión se escribe en la que no se está usando y se arranca con "prueba antes
// de quedarte" (TBYB): si no llega a confirmarse, vuelve sola a la anterior.

// Llamar cuando la vitrina ya ha arrancado bien: confirma la versión nueva
void ota_boot_ok(void);
// Búsqueda automática (si hay WiFi externa y token) y reinicio tras instalar
void ota_tick(void);

bool ota_check(void);    // buscar ahora en GitHub
bool ota_install(void);  // descargar e instalar la versión disponible

// Subida manual de un .uf2 (desde la web). Se llaman desde el contexto de lwIP.
bool ota_upload_begin(uint32_t length, const char **err);
bool ota_upload_data(const uint8_t *data, size_t len, const char **err);
bool ota_upload_end(const char **err);
void ota_upload_abort(const char *err);

// Estado en JSON para la web
int ota_state_json(char *out, size_t max);
