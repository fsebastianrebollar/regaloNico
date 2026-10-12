#include "https_get.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "pico/cyw43_arch.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "mbedtls/ssl.h"
#include "github_ca.h"

#define REQ_MAX       1024
#define HDR_LINE_MAX      256
#define POLL_INTERVAL 2   // ticks de ~0,5 s
#define MAX_IDLE      40  // ~20 s sin recibir nada -> se cancela

static struct altcp_tls_config *tls_config;

static struct {
    bool busy;
    struct altcp_pcb *pcb;
    https_sink_t sink;
    char host[64];
    char req[REQ_MAX];
    int idle;
    // Respuesta
    bool in_body, first_line;
    int status;
    bool chunked;
    long length;           // -1 = desconocido
    uint32_t received;
    char line[HDR_LINE_MAX];
    int line_len;
    bool aborted;          // finish() ha abortado el pcb
} g;

bool https_busy(void) { return g.busy; }

// Termina la petición. Devuelve ERR_ABRT si ha abortado el pcb.
static err_t finish(bool ok, const char *err) {
    err_t ret = ERR_OK;
    if (g.pcb) {
        altcp_arg(g.pcb, NULL);
        altcp_recv(g.pcb, NULL);
        altcp_err(g.pcb, NULL);
        altcp_poll(g.pcb, NULL, 0);
        if (ok && altcp_close(g.pcb) == ERR_OK) {
            ret = ERR_OK;
        } else {
            altcp_abort(g.pcb);
            ret = ERR_ABRT;
            g.aborted = true;
        }
        g.pcb = NULL;
    }
    if (!ok) printf("HTTPS %s: %s\n", g.host, err);
    g.busy = false;
    if (g.sink.on_done) g.sink.on_done(g.sink.ctx, ok, err);
    return ret;
}

// Procesa una línea de cabecera; false = error ya notificado
static bool header_line(const char *l) {
    if (g.first_line) {
        g.first_line = false;
        const char *sp = strchr(l, ' ');
        g.status = sp ? atoi(sp + 1) : 0;
        return true;
    }
    if (!strncasecmp(l, "Content-Length:", 15)) g.length = strtol(l + 15, NULL, 10);
    else if (!strncasecmp(l, "Transfer-Encoding:", 18) && strstr(l + 18, "chunked")) g.chunked = true;
    return true;
}

static const char *status_error(int st) {
    switch (st) {
    case 403:
    case 429: return "GitHub dice que esperes un rato antes de volver a buscar";
    case 404: return "No hay ninguna actualización publicada";
    default:  return "Respuesta inesperada de GitHub";
    }
}

// Cabeceras completas: decide si seguimos
static bool begin_body(void) {
    g.in_body = true;
    if (g.status != 200) {
        printf("HTTPS estado %d\n", g.status);
        finish(false, status_error(g.status));
        return false;
    }
    if (g.chunked) {
        finish(false, "Respuesta troceada no soportada");
        return false;
    }
    if (g.sink.on_begin && !g.sink.on_begin(g.sink.ctx, g.length < 0 ? 0 : (uint32_t)g.length)) {
        finish(false, "Cancelado");
        return false;
    }
    return true;
}

// Devuelve false si la petición ha terminado (con o sin error)
static bool consume(const uint8_t *d, size_t n) {
    while (n && !g.in_body) {
        char ch = (char)*d++;
        n--;
        if (ch == '\n') {
            g.line[g.line_len] = 0;
            if (g.line_len && g.line[g.line_len - 1] == '\r') g.line[--g.line_len] = 0;
            if (g.line_len == 0) {
                if (!begin_body()) return false;
            } else {
                header_line(g.line);
            }
            g.line_len = 0;
        } else if (g.line_len < HDR_LINE_MAX - 1) {
            g.line[g.line_len++] = ch;
        }
    }
    if (!n) return true;
    if (g.length >= 0 && g.received + n > (uint32_t)g.length) n = g.length - g.received;
    g.received += n;
    if (g.sink.on_data && !g.sink.on_data(g.sink.ctx, d, n)) {
        finish(false, "Error guardando los datos");
        return false;
    }
    if (g.length >= 0 && g.received >= (uint32_t)g.length) {
        finish(true, NULL);
        return false;
    }
    return true;
}

static err_t on_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (!g.busy || pcb != g.pcb) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    if (!p) {  // el servidor ha cerrado
        if (g.in_body && g.length < 0 && g.status == 200) return finish(true, NULL);
        return finish(false, "Conexión cortada");
    }
    g.idle = 0;
    altcp_recved(pcb, p->tot_len);
    bool more = true;
    for (struct pbuf *q = p; q && more; q = q->next) more = consume(q->payload, q->len);
    pbuf_free(p);
    return !more && g.aborted ? ERR_ABRT : ERR_OK;
}

static err_t on_poll(void *arg, struct altcp_pcb *pcb) {
    if (++g.idle > MAX_IDLE) return finish(false, "Tiempo agotado");
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    g.pcb = NULL;  // lwIP ya lo ha liberado
    if (g.busy) finish(false, "Error de conexión");
}

static err_t on_connected(void *arg, struct altcp_pcb *pcb, err_t err) {
    if (err != ERR_OK) return finish(false, "No se pudo conectar");
    err = altcp_write(pcb, g.req, strlen(g.req), TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) err = altcp_output(pcb);
    if (err != ERR_OK) return finish(false, "No se pudo enviar la petición");
    return ERR_OK;
}

static void connect_to(const ip_addr_t *addr) {
    if (!tls_config) tls_config = altcp_tls_create_config_client((const u8_t *)github_ca_pem, sizeof github_ca_pem);
    if (!tls_config) { finish(false, "Sin memoria para TLS"); return; }
    g.pcb = altcp_tls_new(tls_config, IPADDR_TYPE_ANY);
    if (!g.pcb) { finish(false, "Sin memoria para TLS"); return; }
    mbedtls_ssl_set_hostname(altcp_tls_context(g.pcb), g.host);  // SNI y verificación del nombre
    altcp_arg(g.pcb, NULL);
    altcp_recv(g.pcb, on_recv);
    altcp_err(g.pcb, on_err);
    altcp_poll(g.pcb, on_poll, POLL_INTERVAL);
    if (altcp_connect(g.pcb, addr, 443, on_connected) != ERR_OK) finish(false, "No se pudo conectar");
}

static void on_dns(const char *name, const ip_addr_t *addr, void *arg) {
    if (!g.busy) return;
    if (!addr) { finish(false, "Sin internet (no se encuentra GitHub)"); return; }
    connect_to(addr);
}

bool https_get(const char *host, const char *path, const char *headers, const https_sink_t *sink) {
    if (g.busy) return false;
    memset(&g, 0, sizeof g);
    g.busy = true;
    g.sink = *sink;
    g.first_line = true;
    g.length = -1;
    strncpy(g.host, host, sizeof g.host - 1);
    int n = snprintf(g.req, REQ_MAX,
                     "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: vitrina-nico\r\n%sConnection: close\r\n\r\n",
                     path, host, headers ? headers : "");
    if (n >= REQ_MAX) { finish(false, "Petición demasiado larga"); return false; }

    cyw43_arch_lwip_begin();
    ip_addr_t addr;
    err_t err = dns_gethostbyname(g.host, &addr, on_dns, NULL);
    if (err == ERR_OK) connect_to(&addr);
    else if (err != ERR_INPROGRESS) finish(false, "Sin internet (DNS)");
    cyw43_arch_lwip_end();
    return true;
}
