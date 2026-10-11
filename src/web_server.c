// Servidor HTTP mínimo sobre la API raw de lwIP.
// Rutas:  /            -> página de control
//         /api/state   -> estado en JSON
//         /api/set?... -> cambia r,g,b,bri,fx,motor y devuelve el estado
//         /api/loco    -> modo loco 15 s (lo termina la propia Pico)
//         /api/stop    -> tarjeta roja: para el motor y apaga las luces
//         /api/button?id=N -> pulsación simulada del botón N (0..1)
//         /api/preset?save=N | apply=N | del=N  (N = 0..4) -> modos de LED guardados
#include "web_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "control.h"
#include "buttons.h"
#include "settings.h"
#include "index_html.h"

#define REQ_MAX 1024
#define HDR_MAX 256
#define BODY_MAX 1024
#define CRAZY_MS 15000

typedef struct {
    struct tcp_pcb *pcb;
    char req[REQ_MAX];
    int req_len;
    bool responded;
    char hdr[HDR_MAX];
    int hdr_left;             // bytes de cabecera pendientes de enviar
    int idle_polls;
    char body[BODY_MAX];      // respuestas dinámicas pequeñas
    const uint8_t *data;      // cuerpo pendiente de enviar
    size_t data_left;
} conn_t;

static bool captive_mode;

static void conn_free(conn_t *c) {
    if (c->pcb) {
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_sent(c->pcb, NULL);
        tcp_err(c->pcb, NULL);
        if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
    }
    free(c);
}

#define POLL_INTERVAL 2   // en ticks de ~0,5 s
#define MAX_IDLE_POLLS 10 // ~10 s sin progreso -> se cierra

// Envía tanto como quepa en el buffer TCP; lo que falte se reintenta en
// on_sent / on_poll
static void send_more(conn_t *c) {
    if (c->hdr_left) {
        int off = (int)strlen(c->hdr) - c->hdr_left;
        u16_t room = tcp_sndbuf(c->pcb);
        u16_t n = c->hdr_left < room ? (u16_t)c->hdr_left : room;
        if (n && tcp_write(c->pcb, c->hdr + off, n, TCP_WRITE_FLAG_COPY) == ERR_OK) c->hdr_left -= n;
        if (c->hdr_left) {
            tcp_output(c->pcb);
            return;
        }
    }
    while (c->data_left) {
        u16_t room = tcp_sndbuf(c->pcb);
        if (!room) break;
        u16_t n = c->data_left < room ? (u16_t)c->data_left : room;
        if (n > TCP_MSS) n = TCP_MSS;
        bool is_static = (const void *)c->data < (const void *)c || (const void *)c->data >= (const void *)(c + 1);
        if (tcp_write(c->pcb, c->data, n, is_static ? 0 : TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        c->data += n;
        c->data_left -= n;
    }
    tcp_output(c->pcb);
}

static void respond(conn_t *c, int code, const char *status, const char *ctype,
                    const void *body, size_t len, const char *extra_hdr) {
    int n = snprintf(c->hdr, HDR_MAX,
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                     "Cache-Control: no-store\r\n%sConnection: close\r\n\r\n",
                     code, status, ctype, (unsigned)len, extra_hdr ? extra_hdr : "");
    c->hdr_left = n;
    c->data = body;
    c->data_left = len;
    c->responded = true;
    send_more(c);
}

static int state_json(char *out, size_t max) {
    control_state_t s = control;
    int n = snprintf(out, max,
                     "{\"r\":%u,\"g\":%u,\"b\":%u,\"bri\":%u,\"fx\":\"%s\",\"motor\":%d,"
                     "\"btn\":[{\"on\":%d,\"n\":%lu},{\"on\":%d,\"n\":%lu}],\"wifi\":\"%s\","
                     "\"loco\":%lu,\"preset\":%d,\"presets\":[",
                     s.r, s.g, s.b, s.brightness, control_effect_name(s.effect), s.motor,
                     buttons[0].pressed, (unsigned long)buttons[0].presses,
                     buttons[1].pressed, (unsigned long)buttons[1].presses,
                     settings_wifi_policy() == WIFI_POLICY_AUTO ? "auto" : "ap",
                     (unsigned long)(control_crazy_left_ms() + 999) / 1000, s.preset);
    for (int i = 0; i < NUM_PRESETS && n < (int)max; i++) {
        const led_preset_t *p = settings_preset(i);
        const char *sep = i ? "," : "";
        if (p)
            n += snprintf(out + n, max - n, "%s{\"r\":%u,\"g\":%u,\"b\":%u,\"fx\":\"%s\"}",
                          sep, p->r, p->g, p->b, control_effect_name(p->fx));
        else
            n += snprintf(out + n, max - n, "%snull", sep);
    }
    if (n < (int)max) n += snprintf(out + n, max - n, "]}");
    return n < (int)max ? n : (int)max - 1;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void apply_query(char *q) {
    bool led_changed = false;
    for (char *kv = strtok(q, "&"); kv; kv = strtok(NULL, "&")) {
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = kv, *v = eq + 1;
        if (!strcmp(k, "r")) {
            control.r = clamp(atoi(v), 0, 255);
            led_changed = true;
        } else if (!strcmp(k, "g")) {
            control.g = clamp(atoi(v), 0, 255);
            led_changed = true;
        } else if (!strcmp(k, "b")) {
            control.b = clamp(atoi(v), 0, 255);
            led_changed = true;
        } else if (!strcmp(k, "fx")) {
            int fx = control_effect_from_name(v);
            if (fx >= 0) {
                control.effect = fx;
                led_changed = true;
            }
        } else if (!strcmp(k, "bri")) {
            control.brightness = clamp(atoi(v), 0, 255);
        } else if (!strcmp(k, "motor")) {
            control.motor = clamp(atoi(v), -100, 100);
        }
    }
    if (led_changed) control.preset = -1;  // ya no coincide con un modo guardado
}

static void apply_preset_query(char *q) {
    char *eq = strchr(q, '=');
    if (!eq) return;
    *eq = 0;
    int i = atoi(eq + 1);
    if (!strcmp(q, "apply") && i == PRESET_OFF) {  // el modo apagado no se guarda ni se borra
        control_apply_preset(i);
        return;
    }
    if (i < 0 || i >= NUM_PRESETS) return;
    if (!strcmp(q, "save")) {
        control_state_t s = control;
        led_preset_t p = { .r = s.r, .g = s.g, .b = s.b, .fx = s.effect };
        settings_set_preset(i, &p);
        control.preset = i;
    } else if (!strcmp(q, "apply")) {
        control_apply_preset(i);
    } else if (!strcmp(q, "del")) {
        settings_set_preset(i, NULL);
        if (control.preset == i) control.preset = -1;
    }
}

static const char *find_header(const char *req, const char *name) {
    size_t n = strlen(name);
    for (const char *p = strstr(req, "\r\n"); p; p = strstr(p + 2, "\r\n"))
        if (!strncasecmp(p + 2, name, n) && p[2 + n] == ':') return p + 3 + n;
    return NULL;
}

// Devuelve el valor de la cabecera Host (sin puerto) en buf
static void get_host(const char *req, char *buf, size_t max) {
    buf[0] = 0;
    const char *h = find_header(req, "Host");
    if (!h) return;
    while (*h == ' ') h++;
    size_t i = 0;
    while (h[i] && h[i] != '\r' && h[i] != ':' && i < max - 1) { buf[i] = h[i]; i++; }
    buf[i] = 0;
}

static void handle_request(conn_t *c) {
    char method[8], path[256], host[64];
    if (sscanf(c->req, "%7s %255s", method, path) != 2) {
        respond(c, 400, "Bad Request", "text/plain", "bad request", 11, NULL);
        return;
    }
    get_host(c->req, host, sizeof host);
    if (strncmp(path, "/api/state", 10) != 0)  // la web lo consulta continuamente
        printf("HTTP %s %s (host %s)\n", method, path, host);

    // En la red propia, cualquier otro dominio (detección de portal cautivo
    // de móviles, etc.) se manda a cumplenico.com
    if (captive_mode && strcasecmp(host, WEB_HOSTNAME) != 0) {
        respond(c, 302, "Found", "text/plain", "", 0, "Location: http://" WEB_HOSTNAME "/\r\n");
        return;
    }

    char *query = strchr(path, '?');
    if (query) *query++ = 0;

    if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
        respond(c, 200, "OK", "text/html; charset=utf-8", index_html, index_html_len, NULL);
    } else if (!strcmp(path, "/api/state")) {
        int n = state_json(c->body, BODY_MAX);
        respond(c, 200, "OK", "application/json", c->body, n, NULL);
    } else if (!strcmp(path, "/api/set")) {
        if (query) apply_query(query);
        int n = state_json(c->body, BODY_MAX);
        respond(c, 200, "OK", "application/json", c->body, n, NULL);
    } else if (!strcmp(path, "/api/loco")) {
        control_crazy_start(CRAZY_MS);
        int n = state_json(c->body, BODY_MAX);
        respond(c, 200, "OK", "application/json", c->body, n, NULL);
    } else if (!strcmp(path, "/api/stop")) {
        control_stop_all();
        int n = state_json(c->body, BODY_MAX);
        respond(c, 200, "OK", "application/json", c->body, n, NULL);
    } else if (!strcmp(path, "/api/button")) {
        if (query && !strncmp(query, "id=", 3)) buttons_inject_click(atoi(query + 3));
        int n = state_json(c->body, BODY_MAX);
        respond(c, 200, "OK", "application/json", c->body, n, NULL);
    } else if (!strcmp(path, "/api/preset")) {
        if (query) apply_preset_query(query);
        int n = state_json(c->body, BODY_MAX);
        respond(c, 200, "OK", "application/json", c->body, n, NULL);
    } else {
        respond(c, 404, "Not Found", "text/plain", "not found", 9, NULL);
    }
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    conn_t *c = arg;
    c->idle_polls = 0;
    if (c->hdr_left || c->data_left) send_more(c);
    else if (c->responded && tcp_sndqueuelen(pcb) == 0) conn_free(c);
    return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    conn_t *c = arg;
    if (!p) {  // el cliente cerró
        conn_free(c);
        return ERR_OK;
    }
    if (!c->responded) {
        u16_t n = MIN(p->tot_len, REQ_MAX - 1 - c->req_len);
        pbuf_copy_partial(p, c->req + c->req_len, n, 0);
        c->req_len += n;
        c->req[c->req_len] = 0;
        if (strstr(c->req, "\r\n\r\n") || c->req_len >= REQ_MAX - 1) handle_request(c);
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
    conn_t *c = arg;
    if (!c) return ERR_OK;
    if (++c->idle_polls > MAX_IDLE_POLLS) {
        tcp_arg(pcb, NULL);
        tcp_abort(pcb);
        free(c);
        return ERR_ABRT;
    }
    if (c->hdr_left || c->data_left) send_more(c);
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    conn_t *c = arg;
    if (c) {
        c->pcb = NULL;  // lwIP ya lo ha liberado
        free(c);
    }
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    if (err != ERR_OK || !pcb) return ERR_VAL;
    conn_t *c = calloc(1, sizeof *c);
    if (!c) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    c->pcb = pcb;
    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
    tcp_poll(pcb, on_poll, POLL_INTERVAL);
    return ERR_OK;
}

bool web_server_start(bool captive) {
    captive_mode = captive;
    cyw43_arch_lwip_begin();
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    bool ok = pcb && tcp_bind(pcb, IP_ANY_TYPE, 80) == ERR_OK;
    if (ok) {
        pcb = tcp_listen_with_backlog(pcb, 8);
        tcp_accept(pcb, on_accept);
    }
    cyw43_arch_lwip_end();
    printf(ok ? "Web lista en el puerto 80\n" : "No se pudo arrancar la web\n");
    return ok;
}
