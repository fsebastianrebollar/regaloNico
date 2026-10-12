#pragma once
#include <stddef.h>

// Alexa: la vitrina se anuncia como una bombilla Philips Hue llamada "España"
// (las Echo la encuentran sin instalar ninguna skill). "Alexa, enciende España"
// lanza el ¡gol!; "apaga España" lo corta. Solo en una WiFi externa.
#define ALEXA_NAME   "España"
#define ALEXA_GOL_MS 7000

void alexa_tick(void);  // arranca el descubrimiento (SSDP) cuando hay WiFi externa

// Peticiones HTTP del protocolo Hue. Devuelve la longitud de la respuesta
// escrita en out o -1 si la ruta no es de Hue. *xml = true si es XML.
int alexa_http(const char *method, const char *path, const char *body, char *out, size_t max, int *xml);
