#include "settings.h"
#include <string.h>
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "wifi_secrets.h"

#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC  0x4E49434Fu  // "NICO"
#define PRESETS_MAGIC   0x50524553u  // "PRES" (añadido después: se valida aparte)
#define WIFI_MAGIC      0x57494649u  // "WIFI" (ídem)
#define OTA_MAGIC       0x4F544131u  // "OTA1" (ídem)

typedef struct {
    uint32_t magic;
    uint32_t old_wifi_policy;  // versión anterior: 0 = SEABASS si está, 1 = red propia
    uint32_t presets_magic;
    led_preset_t presets[NUM_PRESETS];
    uint32_t wifi_magic;
    char wifi_ssid[WIFI_SSID_MAX + 1];
    char wifi_pass[WIFI_PASS_MAX + 1];
    uint32_t ota_magic;
    uint8_t auto_update;
} settings_t;

static_assert(sizeof(settings_t) <= FLASH_PAGE_SIZE, "settings no cabe en una página");

static settings_t cur;
static volatile bool dirty;

void settings_init(void) {
    // Sin traducción de direcciones: con particiones A/B, XIP_BASE apunta al
    // principio de la partición en uso, no al de la flash
    const settings_t *s = (const settings_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + SETTINGS_OFFSET);
    memset(&cur, 0, sizeof cur);
    cur.magic = SETTINGS_MAGIC;
    cur.presets_magic = PRESETS_MAGIC;
    cur.wifi_magic = WIFI_MAGIC;
    cur.ota_magic = OTA_MAGIC;
    if (s->magic != SETTINGS_MAGIC) return;
    if (s->presets_magic == PRESETS_MAGIC)
        memcpy(cur.presets, s->presets, sizeof cur.presets);
    if (s->ota_magic == OTA_MAGIC) cur.auto_update = s->auto_update ? 1 : 0;
    if (s->wifi_magic == WIFI_MAGIC) {
        memcpy(cur.wifi_ssid, s->wifi_ssid, sizeof cur.wifi_ssid);
        memcpy(cur.wifi_pass, s->wifi_pass, sizeof cur.wifi_pass);
        cur.wifi_ssid[WIFI_SSID_MAX] = cur.wifi_pass[WIFI_PASS_MAX] = 0;
    }
#ifdef HOME_WIFI_SSID
    else if (s->old_wifi_policy == 0) {
        // Viene de la versión con WiFi de casa automática: se conserva esa red
        settings_set_wifi(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);
    }
#endif
}

static void write_sector(void *page) {
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);
}

bool settings_save_if_dirty(void) {
    if (!dirty) return true;
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof page);
    uint32_t irq = save_and_disable_interrupts();  // copia coherente frente a la web
    memcpy(page, &cur, sizeof cur);
    dirty = false;
    restore_interrupts(irq);
    if (flash_safe_execute(write_sector, page, 500) != PICO_OK) {
        dirty = true;
        return false;
    }
    return true;
}

bool settings_auto_update(void) { return cur.auto_update; }

void settings_set_auto_update(bool on) {
    if (cur.auto_update == on) return;
    cur.auto_update = on;
    dirty = true;
}

const char *settings_wifi_ssid(void) { return cur.wifi_ssid; }
const char *settings_wifi_pass(void) { return cur.wifi_pass; }

void settings_set_wifi(const char *ssid, const char *pass) {
    memset(cur.wifi_ssid, 0, sizeof cur.wifi_ssid);
    memset(cur.wifi_pass, 0, sizeof cur.wifi_pass);
    strncpy(cur.wifi_ssid, ssid, WIFI_SSID_MAX);
    strncpy(cur.wifi_pass, pass, WIFI_PASS_MAX);
    dirty = true;
}

const led_preset_t *settings_preset(int i) {
    if (i < 0 || i >= NUM_PRESETS || !cur.presets[i].used) return NULL;
    return &cur.presets[i];
}

void settings_set_preset(int i, const led_preset_t *p) {
    if (i < 0 || i >= NUM_PRESETS) return;
    if (p) {
        cur.presets[i] = *p;
        cur.presets[i].used = 1;
    } else {
        memset(&cur.presets[i], 0, sizeof cur.presets[i]);
    }
    dirty = true;
}
