#pragma once
#include <stdbool.h>

typedef enum { WIFI_MODE_NONE, WIFI_MODE_STA, WIFI_MODE_AP } wifi_mode_t;

// Si hay una WiFi configurada (settings_wifi_ssid) se conecta a ella y lo
// reintenta sin fin desde wifi_tick(); si no, crea la red propia.
// No bloquea: el resto de la vitrina funciona mientras conecta.
wifi_mode_t wifi_start(void);
void wifi_tick(void);  // llamar a menudo desde el bucle principal
bool wifi_connected(void);
bool wifi_online(void);  // conectada a una WiFi externa (con posible salida a internet)
const char *wifi_ip(void);
