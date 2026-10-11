#include "settings.h"
#include <string.h>
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC  0x4E49434Fu  // "NICO"
#define PRESETS_MAGIC   0x50524553u  // "PRES" (añadido después: se valida aparte)

typedef struct {
    uint32_t magic;
    uint32_t wifi_policy;
    uint32_t presets_magic;
    led_preset_t presets[NUM_PRESETS];
} settings_t;

static_assert(sizeof(settings_t) <= FLASH_PAGE_SIZE, "settings no cabe en una página");

static settings_t cur;
static volatile bool dirty;

void settings_init(void) {
    const settings_t *s = (const settings_t *)(XIP_BASE + SETTINGS_OFFSET);
    memset(&cur, 0, sizeof cur);
    cur.magic = SETTINGS_MAGIC;
    cur.presets_magic = PRESETS_MAGIC;
    if (s->magic == SETTINGS_MAGIC && s->wifi_policy <= WIFI_POLICY_AP_ONLY)
        cur.wifi_policy = s->wifi_policy;
    if (s->magic == SETTINGS_MAGIC && s->presets_magic == PRESETS_MAGIC)
        memcpy(cur.presets, s->presets, sizeof cur.presets);
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

wifi_policy_t settings_wifi_policy(void) { return (wifi_policy_t)cur.wifi_policy; }

void settings_set_wifi_policy(wifi_policy_t p) {
    cur.wifi_policy = p;
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
