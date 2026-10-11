#pragma once
#include <stdbool.h>

// Servidor HTTP en el puerto 80. Con captive=true (red propia) cualquier
// petición a otro host se redirige a http://<WEB_HOSTNAME>/
#define WEB_HOSTNAME "cumplenico.com"

bool web_server_start(bool captive);
