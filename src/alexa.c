#include "alexa.h"
#include <stdio.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "control.h"
#include "wifi.h"

#define SSDP_PORT 1900
#define LIGHT_ID  "1"

static struct udp_pcb *ssdp;

static void mac_str(char *out, bool colons) {
    uint8_t m[6];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, m);
    if (colons) sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    else        sprintf(out, "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

// Respuesta a la búsqueda de dispositivos de la Echo (UPnP/SSDP)
static void on_ssdp(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    char req[512];
    u16_t n = pbuf_copy_partial(p, req, sizeof req - 1, 0);
    req[n] = 0;
    pbuf_free(p);
    if (strncmp(req, "M-SEARCH", 8) || !strstr(req, "ssdp:discover")) return;
    if (!strstr(req, "device:basic:1") && !strstr(req, "ssdp:all") && !strstr(req, "upnp:rootdevice")) return;

    char mac[13], resp[512];
    mac_str(mac, false);
    int len = snprintf(resp, sizeof resp,
                       "HTTP/1.1 200 OK\r\n"
                       "EXT:\r\n"
                       "CACHE-CONTROL: max-age=100\r\n"
                       "LOCATION: http://%s:80/description.xml\r\n"
                       "SERVER: FreeRTOS/6.0.5, UPnP/1.0, IpBridge/1.17.0\r\n"
                       "hue-bridgeid: %s\r\n"
                       "ST: urn:schemas-upnp-org:device:basic:1\r\n"
                       "USN: uuid:2f402f80-da50-11e1-9b23-%s::upnp:rootdevice\r\n"
                       "\r\n",
                       wifi_ip(), mac, mac);
    struct pbuf *out = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (!out) return;
    memcpy(out->payload, resp, len);
    udp_sendto(pcb, out, addr, port);
    pbuf_free(out);
}

void alexa_tick(void) {
    if (ssdp || !wifi_online()) return;
    cyw43_arch_lwip_begin();
    ssdp = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (ssdp && udp_bind(ssdp, IP_ANY_TYPE, SSDP_PORT) == ERR_OK) {
        udp_recv(ssdp, on_ssdp, NULL);
        ip4_addr_t group;
        IP4_ADDR(&group, 239, 255, 255, 250);
        igmp_joingroup_netif(&cyw43_state.netif[CYW43_ITF_STA], &group);
        printf("Alexa: anunciando '%s'\n", ALEXA_NAME);
    }
    cyw43_arch_lwip_end();
}

static int light_json(char *out, size_t max) {
    char mac[18];
    mac_str(mac, true);
    bool on = control_party_left_ms(PARTY_GOL) > 0;
    return snprintf(out, max,
                    "{\"state\":{\"on\":%s,\"bri\":254,\"alert\":\"none\",\"mode\":\"homeautomation\",\"reachable\":true},"
                    "\"type\":\"Dimmable light\",\"name\":\"" ALEXA_NAME "\",\"modelid\":\"LWB010\","
                    "\"manufacturername\":\"Philips\",\"productname\":\"E1\","
                    "\"uniqueid\":\"%s:00:11-01\",\"swversion\":\"vitrina-%d\"}",
                    on ? "true" : "false", mac, FW_VERSION);
}

static int description_xml(char *out, size_t max) {
    char mac[13];
    mac_str(mac, false);
    const char *ip = wifi_ip();
    return snprintf(out, max,
                    "<?xml version=\"1.0\" ?><root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
                    "<specVersion><major>1</major><minor>0</minor></specVersion>"
                    "<URLBase>http://%s:80/</URLBase><device>"
                    "<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
                    "<friendlyName>Vitrina de Nico (%s)</friendlyName>"
                    "<manufacturer>Royal Philips Electronics</manufacturer>"
                    "<manufacturerURL>http://www.philips.com</manufacturerURL>"
                    "<modelDescription>Philips hue Personal Wireless Lighting</modelDescription>"
                    "<modelName>Philips hue bridge 2012</modelName><modelNumber>929000226503</modelNumber>"
                    "<modelURL>http://www.meethue.com</modelURL><serialNumber>%s</serialNumber>"
                    "<UDN>uuid:2f402f80-da50-11e1-9b23-%s</UDN><presentationURL>index.html</presentationURL>"
                    "</device></root>",
                    ip, ip, mac, mac);
}

// Busca "key": true/false en un JSON sencillo; -1 si no está
static int json_bool(const char *body, const char *key) {
    const char *k = body ? strstr(body, key) : NULL;
    if (!k) return -1;
    k = strchr(k + strlen(key), ':');
    if (!k) return -1;
    while (*++k == ' ') {}
    return !strncmp(k, "true", 4) ? 1 : !strncmp(k, "false", 5) ? 0 : -1;
}

int alexa_http(const char *method, const char *path, const char *body, char *out, size_t max, int *xml) {
    *xml = 0;
    if (!strcmp(path, "/description.xml")) {
        *xml = 1;
        return description_xml(out, max);
    }
    if (strncmp(path, "/api", 4) || (path[4] && path[4] != '/')) return -1;

    // /api/<usuario>/lights/<id>/state  (el usuario da igual)
    const char *rest = path + 4;
    if (*rest == '/') rest++;
    if (!*rest) {  // alta de usuario
        return snprintf(out, max, "[{\"success\":{\"username\":\"vitrinanico\"}}]");
    }
    const char *lights = strchr(rest, '/');
    if (!lights) {  // /api/<usuario>: configuración completa
        int n = snprintf(out, max, "{\"lights\":{\"" LIGHT_ID "\":");
        n += light_json(out + n, max - n);
        return n + snprintf(out + n, max - n, "}}");
    }
    if (strncmp(lights, "/lights", 7)) return snprintf(out, max, "{}");
    const char *id = lights + 7;
    if (!*id || !strcmp(id, "/")) {
        int n = snprintf(out, max, "{\"" LIGHT_ID "\":");
        n += light_json(out + n, max - n);
        return n + snprintf(out + n, max - n, "}");
    }
    if (strncmp(id, "/" LIGHT_ID, 2) || (id[2] && id[2] != '/')) return snprintf(out, max, "[]");
    if (!id[2]) return light_json(out, max);

    if (!strcmp(id + 2, "/state") && (!strcmp(method, "PUT") || !strcmp(method, "POST"))) {
        int on = json_bool(body, "\"on\"");
        if (on == 1) {
            printf("Alexa: enciende " ALEXA_NAME " -> ¡gol!\n");
            control_gol_start(ALEXA_GOL_MS);
        } else if (on == 0) {
            printf("Alexa: apaga " ALEXA_NAME "\n");
            control_party_stop(PARTY_GOL);
        }
        return snprintf(out, max, "[{\"success\":{\"/lights/" LIGHT_ID "/state/on\":%s}}]",
                        on == 0 ? "false" : "true");
    }
    return snprintf(out, max, "[]");
}
