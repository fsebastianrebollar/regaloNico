#include "wifi.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "settings.h"
#include "wifi_secrets.h"

#define STA_CONNECT_TIMEOUT_MS 20000
#define STA_RETRY_PAUSE_MS     2000
#define STA_LOST_MS            10000  // sin enlace este tiempo -> se reconecta

static dhcp_server_t dhcp_server;
static dns_server_t dns_server;
static wifi_mode_t mode = WIFI_MODE_NONE;

static enum { STA_IDLE, STA_CONNECTING, STA_UP, STA_PAUSE } sta;
static absolute_time_t sta_deadline;
static uint32_t attempts;

// Va enchufado, no a batería: sin ahorro de energía la web responde mucho más
// rápido. Hay que aplicarlo tras activar cada modo (activarlo lo resetea).
static void disable_power_save(void) {
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
}

static void sta_connect(void) {
    const char *pass = settings_wifi_pass();
    uint32_t auth = pass[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
    printf("Conectando a '%s' (intento %lu)...\n", settings_wifi_ssid(), (unsigned long)++attempts);
    if (cyw43_arch_wifi_connect_async(settings_wifi_ssid(), pass[0] ? pass : NULL, auth)) {
        sta = STA_PAUSE;
        sta_deadline = make_timeout_time_ms(STA_RETRY_PAUSE_MS);
        return;
    }
    sta = STA_CONNECTING;
    sta_deadline = make_timeout_time_ms(STA_CONNECT_TIMEOUT_MS);
}

static void sta_retry_later(void) {
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    sta = STA_PAUSE;
    sta_deadline = make_timeout_time_ms(STA_RETRY_PAUSE_MS);
}

void wifi_tick(void) {
    static uint32_t last;
    if (mode != WIFI_MODE_STA) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last < 100) return;
    last = now;

    int st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    switch (sta) {
    case STA_CONNECTING:
        if (st == CYW43_LINK_UP) {
            sta = STA_UP;
            printf("Conectado a '%s', IP %s\n", settings_wifi_ssid(), wifi_ip());
        } else if (st == CYW43_LINK_NONET || st == CYW43_LINK_BADAUTH || st == CYW43_LINK_FAIL) {
            printf("No se puede conectar (estado %d)\n", st);
            sta_retry_later();
        } else if (time_reached(sta_deadline)) {
            printf("Tiempo agotado conectando\n");
            sta_retry_later();
        }
        break;
    case STA_UP:
        if (st == CYW43_LINK_UP) {
            sta_deadline = make_timeout_time_ms(STA_LOST_MS);
        } else if (time_reached(sta_deadline)) {
            printf("WiFi perdida (estado %d), reconectando\n", st);
            sta_retry_later();
        }
        break;
    case STA_PAUSE:
        if (time_reached(sta_deadline)) sta_connect();
        break;
    default:
        break;
    }
}

static void start_access_point(void) {
    cyw43_arch_enable_ap_mode(AP_WIFI_SSID, AP_WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
    disable_power_save();

    // El driver ya asigna 192.168.4.1/24 a la interfaz AP
    ip_addr_t gw, mask;
    ip_addr_set_ip4_u32(&gw, PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS));
    ip_addr_set_ip4_u32(&mask, PP_HTONL(CYW43_DEFAULT_IP_MASK));
    dhcp_server_init(&dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);
    // Responde a cualquier nombre con nuestra IP: así cumplenico.com lleva aquí
    dns_server_init(&dns_server, &cyw43_state.netif[CYW43_ITF_AP], &gw);
}

wifi_mode_t wifi_start(void) {
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_SPAIN)) {
        printf("Fallo al inicializar cyw43_arch\n");
        return mode = WIFI_MODE_NONE;
    }
    if (settings_wifi_ssid()[0]) {
        cyw43_arch_enable_sta_mode();
        disable_power_save();
        mode = WIFI_MODE_STA;
        sta_connect();
        return mode;
    }
    start_access_point();
    mode = WIFI_MODE_AP;
    printf("Red propia '%s' creada, IP %s\n", AP_WIFI_SSID, wifi_ip());
    return mode;
}

bool wifi_connected(void) {
    return mode == WIFI_MODE_AP || (mode == WIFI_MODE_STA && sta == STA_UP);
}

bool wifi_online(void) {
    return mode == WIFI_MODE_STA && sta == STA_UP;
}

const char *wifi_ip(void) {
    int itf = mode == WIFI_MODE_AP ? CYW43_ITF_AP : CYW43_ITF_STA;
    return ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[itf]));
}
