#include "wifi.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "wifi_secrets.h"

#define STA_CONNECT_TIMEOUT_MS 20000
#define STA_CONNECT_ATTEMPTS   3

static dhcp_server_t dhcp_server;
static dns_server_t dns_server;
static wifi_mode_t mode = WIFI_MODE_NONE;

// Va enchufado, no a batería: sin ahorro de energía la web responde mucho más
// rápido. Hay que aplicarlo tras activar cada modo (activarlo lo resetea).
static void disable_power_save(void) {
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
}

static bool connect_attempt(void (*on_progress)(void)) {
    if (cyw43_arch_wifi_connect_async(HOME_WIFI_SSID, HOME_WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK)) return false;

    absolute_time_t deadline = make_timeout_time_ms(STA_CONNECT_TIMEOUT_MS);
    while (!time_reached(deadline)) {
        int st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (st == CYW43_LINK_UP) return true;
        if (st == CYW43_LINK_NONET || st == CYW43_LINK_BADAUTH || st == CYW43_LINK_FAIL) {
            printf("No se puede conectar (estado %d)\n", st);
            return false;
        }
        if (on_progress) on_progress();
        sleep_ms(100);
    }
    printf("Tiempo agotado conectando\n");
    return false;
}

// Varios intentos: un fallo puntual no debe dejarnos en la red propia
static bool try_home_wifi(void (*on_progress)(void)) {
    cyw43_arch_enable_sta_mode();
    disable_power_save();
    for (int i = 1; i <= STA_CONNECT_ATTEMPTS; i++) {
        printf("Conectando a '%s' (intento %d/%d)...\n", HOME_WIFI_SSID, i, STA_CONNECT_ATTEMPTS);
        if (connect_attempt(on_progress)) return true;
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        for (int t = 0; t < 10; t++) {  // ~1 s de pausa sin congelar la animación
            if (on_progress) on_progress();
            sleep_ms(100);
        }
    }
    return false;
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

wifi_mode_t wifi_start(wifi_policy_t policy, void (*on_progress)(void)) {
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_SPAIN)) {
        printf("Fallo al inicializar cyw43_arch\n");
        return mode = WIFI_MODE_NONE;
    }
    if (policy == WIFI_POLICY_AUTO && try_home_wifi(on_progress)) {
        mode = WIFI_MODE_STA;
        printf("Conectado a '%s', IP %s\n", HOME_WIFI_SSID, wifi_ip());
        return mode;
    }
    if (policy == WIFI_POLICY_AUTO) cyw43_arch_disable_sta_mode();
    else printf("Modo solo red propia\n");
    start_access_point();
    mode = WIFI_MODE_AP;
    printf("Red propia '%s' creada, IP %s\n", AP_WIFI_SSID, wifi_ip());
    return mode;
}

const char *wifi_ip(void) {
    int itf = mode == WIFI_MODE_AP ? CYW43_ITF_AP : CYW43_ITF_STA;
    return ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[itf]));
}
