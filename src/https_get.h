#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Cliente HTTPS mínimo (una petición GET cada vez) sobre lwIP + Mbed TLS.
// Los callbacks se llaman desde el contexto de lwIP.
typedef struct {
    // Antes del cuerpo, con el Content-Length. false = cancelar
    bool (*on_begin)(void *ctx, uint32_t length);
    // Trozo del cuerpo. false = cancelar
    bool (*on_data)(void *ctx, const uint8_t *data, size_t len);
    // Fin: ok = cuerpo completo; si no, err explica el motivo
    void (*on_done)(void *ctx, bool ok, const char *err);
    void *ctx;
} https_sink_t;

// headers: líneas extra ya terminadas en "\r\n" (o NULL). Se copian.
// Devuelve false si ya hay una petición en curso o no se puede empezar.
bool https_get(const char *host, const char *path, const char *headers, const https_sink_t *sink);
bool https_busy(void);
