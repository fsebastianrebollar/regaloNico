#include "ota.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/structs/qmi.h"
#include "boot/picobin.h"
#include "boot/uf2.h"
#include "boot/picoboot_constants.h"
#include "https_get.h"
#include "wifi.h"
#include "leds.h"
#include "motor.h"
#include "settings.h"
#include "control.h"

#define GH_HOST       "api.github.com"
#define GH_REPO       "fsebastianrebollar/regaloNico"
#define GH_BRANCH     "firmware"
#define GH_PATH(f)    "/repos/" GH_REPO "/contents/" f "?ref=" GH_BRANCH

#define FIRST_CHECK_MS  10000                 // tras conectar a la WiFi
#define CHECK_EVERY_MS  (6u * 60 * 60 * 1000) // luego cada 6 h
#define RETRY_MS        (30u * 60 * 1000)     // si falla, a la media hora
#define REBOOT_DELAY_MS 1500                  // para que la web reciba la respuesta
#define BUY_ONLINE_MS   3000                  // versión nueva: confirmarla tras 3 s en la WiFi...
#define BUY_AP_MS       8000                  // ...o 8 s en la red propia...
#define BUY_LATEST_MS   14000                 // ...y como muy tarde a los 14 s (la bootrom espera 16,7)
#define WATCHDOG_MS     8000                  // si el programa se cuelga, se reinicia solo
#define AUTO_RETRY_MS   (10u * 60 * 1000)     // instalación automática fallida: otro intento a los 10 min

typedef enum {
    ST_IDLE, ST_CHECKING, ST_UP_TO_DATE, ST_AVAILABLE, ST_DOWNLOADING, ST_UPLOADING, ST_DONE, ST_ERROR,
} ota_state_t;

static const char *const state_names[] = {
    "idle", "checking", "uptodate", "available", "downloading", "uploading", "done", "error",
};

static volatile ota_state_t state;
static volatile int latest = -1;          // última versión publicada (-1 = no se sabe)
static const char *volatile error_msg = "";
static absolute_time_t reboot_at;

// Zona de trabajo para la bootrom (al menos 4 KB, alineada)
static __attribute__((aligned(4))) uint8_t workarea[4096];

/* ---------- Escritura del UF2 en la partición libre ---------- */

static struct {
    bool active;
    uint32_t part_start, part_size;  // offsets físicos en la flash
    uint8_t blk[512];
    int blk_len;
    int32_t sec_off;                 // sector en curso (offset en la partición), -1 = ninguno
    int32_t last_written;            // último sector escrito
    uint8_t sec[FLASH_SECTOR_SIZE];
    uint8_t first[FLASH_SECTOR_SIZE];// el primero se escribe al final: sin él no arranca
    uint32_t blocks_seen, num_blocks;
    uint32_t received, total;
} w;

typedef struct { uint32_t off; const uint8_t *data; bool program; } flash_op_t;

static void do_flash_op(void *param) {
    const flash_op_t *op = param;
    flash_range_erase(op->off, FLASH_SECTOR_SIZE);
    if (op->program) flash_range_program(op->off, op->data, FLASH_SECTOR_SIZE);
}

static bool write_sector(uint32_t off, const uint8_t *data) {
    flash_op_t op = { .off = off, .data = data, .program = data != NULL };
    return flash_safe_execute(do_flash_op, &op, 500) == PICO_OK;
}

static bool flush_sector(const char **err) {
    if (w.sec_off < 0) return true;
    if (w.sec_off == 0) {
        memcpy(w.first, w.sec, sizeof w.first);
    } else {
        if (w.sec_off <= w.last_written) { *err = "El archivo .uf2 está desordenado"; return false; }
        if (!write_sector(w.part_start + w.sec_off, w.sec)) { *err = "Error escribiendo la flash"; return false; }
        w.last_written = w.sec_off;
    }
    w.sec_off = -1;
    return true;
}

static bool process_block(const struct uf2_block *b, const char **err) {
    if (b->magic_start0 != UF2_MAGIC_START0 || b->magic_start1 != UF2_MAGIC_START1 || b->magic_end != UF2_MAGIC_END) {
        *err = "El archivo no es un .uf2 válido";
        return false;
    }
    // Solo cuentan los bloques del programa (no el de la errata E10 ni otros)
    if (b->flags & UF2_FLAG_NOT_MAIN_FLASH) return true;
    if (!(b->flags & UF2_FLAG_FAMILY_ID_PRESENT) || b->file_size != RP2350_ARM_S_FAMILY_ID) return true;

    uint32_t off = b->target_addr - XIP_BASE;
    if (b->target_addr < XIP_BASE || b->payload_size != 256 || (off & 255) || off + 256 > w.part_size) {
        *err = "El .uf2 no es para la vitrina";
        return false;
    }
    w.num_blocks = b->num_blocks;
    w.blocks_seen++;
    int32_t sec = (int32_t)(off & ~(FLASH_SECTOR_SIZE - 1));
    if (sec != w.sec_off) {
        if (!flush_sector(err)) return false;
        w.sec_off = sec;
        memset(w.sec, 0xff, sizeof w.sec);
    }
    memcpy(w.sec + (off & (FLASH_SECTOR_SIZE - 1)), b->data, 256);
    return true;
}

static bool writer_begin(uint32_t total, const char **err) {
    if (w.active) { *err = "Ya hay una actualización en marcha"; return false; }

    resident_partition_t part;
    rom_flash_flush_cache();
    if (rom_get_uf2_target_partition(workarea, sizeof workarea, RP2350_ARM_S_FAMILY_ID, &part) < 0) {
        *err = "La flash no tiene particiones A/B (hay que prepararla por USB una vez)";
        return false;
    }
    uint32_t first = (part.permissions_and_location & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS)
                     >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    uint32_t last = (part.permissions_and_location & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS)
                    >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    uint32_t running = (qmi_hw->atrans[0] & QMI_ATRANS0_BASE_BITS) * FLASH_SECTOR_SIZE;
    if (first * FLASH_SECTOR_SIZE == running) {
        *err = "No hay partición libre";
        return false;
    }

    memset(&w, 0, sizeof w);
    w.part_start = first * FLASH_SECTOR_SIZE;
    w.part_size = (last + 1 - first) * FLASH_SECTOR_SIZE;
    w.sec_off = -1;
    w.last_written = 0;
    w.total = total;
    if (total / 2 > w.part_size) { *err = "El archivo es demasiado grande"; return false; }
    // Primero se invalida lo que hubiera: una copia a medias nunca arranca
    if (!write_sector(w.part_start, NULL)) { *err = "Error escribiendo la flash"; return false; }
    w.active = true;
    printf("Actualización: escribiendo en 0x%06lx (%lu KB)\n", (unsigned long)w.part_start,
           (unsigned long)(w.part_size / 1024));
    return true;
}

static bool writer_data(const uint8_t *d, size_t n, const char **err) {
    if (!w.active) { *err = "No hay actualización en marcha"; return false; }
    w.received += n;
    while (n) {
        size_t k = MIN(n, sizeof w.blk - w.blk_len);
        memcpy(w.blk + w.blk_len, d, k);
        w.blk_len += k; d += k; n -= k;
        if (w.blk_len == sizeof w.blk) {
            w.blk_len = 0;
            if (!process_block((const struct uf2_block *)w.blk, err)) return false;
        }
    }
    return true;
}

static void writer_abort(void) {
    if (!w.active) return;
    w.active = false;
    write_sector(w.part_start, NULL);  // por si acaso, que no arranque
}

static bool writer_end(const char **err) {
    if (!w.active) { *err = "No hay actualización en marcha"; return false; }
    if (!flush_sector(err)) return false;
    if (w.blk_len || !w.num_blocks || w.blocks_seen != w.num_blocks) {
        *err = "El archivo está incompleto";
        return false;
    }
    if (!write_sector(w.part_start, w.first)) { *err = "Error escribiendo la flash"; return false; }
    w.active = false;
    printf("Actualización escrita (%lu bloques)\n", (unsigned long)w.blocks_seen);
    return true;
}

static void fail(const char *err) {
    writer_abort();
    error_msg = err ? err : "Error";
    state = ST_ERROR;
    printf("Actualización fallida: %s\n", error_msg);
}

static void finished(void) {
    state = ST_DONE;
    reboot_at = make_timeout_time_ms(REBOOT_DELAY_MS);
}

/* ---------- Subida manual desde la web ---------- */

bool ota_upload_begin(uint32_t length, const char **err) {
    if (state == ST_DOWNLOADING || state == ST_UPLOADING || state == ST_DONE || https_busy()) {
        *err = "Ya hay una actualización en marcha";
        return false;
    }
    if (!writer_begin(length, err)) return false;
    state = ST_UPLOADING;
    return true;
}

bool ota_upload_data(const uint8_t *data, size_t len, const char **err) {
    if (writer_data(data, len, err)) return true;
    fail(*err);
    return false;
}

bool ota_upload_end(const char **err) {
    if (!writer_end(err)) {
        fail(*err);
        return false;
    }
    finished();
    return true;
}

void ota_upload_abort(const char *err) {
    if (state == ST_UPLOADING) fail(err);
}

/* ---------- GitHub ---------- */

// El repositorio es público: no hace falta token
#define GH_HEADERS "Accept: application/vnd.github.raw\r\n" \
                   "X-GitHub-Api-Version: 2022-11-28\r\n"

static uint32_t next_check;  // ms desde el arranque (0 = sin programar)

static void schedule_check(uint32_t in_ms) {
    next_check = to_ms_since_boot(get_absolute_time()) + in_ms;
    if (!next_check) next_check = 1;
}

static char version_buf[16];
static size_t version_len;

static bool version_data(void *ctx, const uint8_t *d, size_t n) {
    size_t k = MIN(n, sizeof version_buf - 1 - version_len);
    memcpy(version_buf + version_len, d, k);
    version_len += k;
    return true;
}

static void version_done(void *ctx, bool ok, const char *err) {
    if (!ok) {
        error_msg = err;
        state = ST_ERROR;
        schedule_check(RETRY_MS);
        return;
    }
    version_buf[version_len] = 0;
    latest = atoi(version_buf);
    state = latest > FW_VERSION ? ST_AVAILABLE : ST_UP_TO_DATE;
    printf("Versión publicada: %d (instalada %d)\n", latest, FW_VERSION);
    schedule_check(CHECK_EVERY_MS);
}

bool ota_check(void) {
    if (state == ST_CHECKING || state == ST_DOWNLOADING || state == ST_UPLOADING || state == ST_DONE) return false;
    if (!wifi_online()) {
        error_msg = "Para buscar actualizaciones la vitrina tiene que estar en una WiFi con internet";
        state = ST_ERROR;
        return false;
    }
    version_len = 0;
    static const https_sink_t sink = { .on_data = version_data, .on_done = version_done };
    state = ST_CHECKING;
    if (!https_get(GH_HOST, GH_PATH("version.txt"), GH_HEADERS, &sink)) {
        state = ST_ERROR;
        error_msg = "Ocupada, prueba otra vez";
        return false;
    }
    return true;
}

static const char *dl_err;  // fallo al escribir (más útil que el de la conexión)

static bool download_begin(void *ctx, uint32_t length) {
    return writer_begin(length, &dl_err);
}

static bool download_data(void *ctx, const uint8_t *d, size_t n) {
    return writer_data(d, n, &dl_err);
}

static void download_done(void *ctx, bool ok, const char *err) {
    if (ok && writer_end(&dl_err)) {
        finished();
        return;
    }
    fail(dl_err ? dl_err : err);
}

bool ota_install(void) {
    if (state != ST_AVAILABLE) return false;
    static const https_sink_t sink = { .on_begin = download_begin, .on_data = download_data, .on_done = download_done };
    dl_err = NULL;
    state = ST_DOWNLOADING;
    if (!https_get(GH_HOST, GH_PATH("regalo_nico.uf2"), GH_HEADERS, &sink)) {
        state = ST_AVAILABLE;
        return false;
    }
    return true;
}

/* ---------- Arranque y bucle ---------- */

static bool buy_pending, watchdog_on;

void ota_boot_ok(void) {
    boot_info_t bi = {0};
    rom_get_boot_info(&bi);
    printf("Firmware v%d en la partición %d\n", FW_VERSION, bi.partition);
    buy_pending = rom_get_last_boot_type() == BOOT_TYPE_FLASH_UPDATE;
}

// Una versión recién instalada no se confirma hasta ver que aguanta conectada
// a la WiFi: si se cuelga antes, la bootrom vuelve sola a la anterior. Mientras
// tanto no se toca el watchdog (es el que usa la bootrom para esa vuelta atrás).
static void confirm_tick(void) {
    static uint32_t online_since;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (watchdog_on) {
        watchdog_update();
        return;
    }
    if (buy_pending) {
        if (!wifi_online()) online_since = 0;
        else if (!online_since) online_since = now;
        bool ok = (online_since && now - online_since >= BUY_ONLINE_MS) ||
                  (wifi_mode() == WIFI_MODE_AP && now >= BUY_AP_MS) || now >= BUY_LATEST_MS;
        if (!ok) return;
        int rc = rom_explicit_buy(workarea, sizeof workarea);
        printf("Versión nueva confirmada (%d)\n", rc);
        buy_pending = false;
    }
    watchdog_enable(WATCHDOG_MS, true);
    watchdog_on = true;
}

// Con la casilla marcada, una versión nueva se instala sola (sin cortar un
// ¡gol! o el modo loco que esté en marcha)
static void auto_install_tick(void) {
    static uint32_t last_try;
    static bool tried;
    if (!settings_auto_update() || state != ST_AVAILABLE) return;
    if (control_party_left_ms(PARTY_GOL) || control_party_left_ms(PARTY_CRAZY)) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (tried && now - last_try < AUTO_RETRY_MS) return;
    tried = true;
    last_try = now;
    printf("Instalación automática de la v%d\n", latest);
    ota_install();
}

void ota_tick(void) {
    confirm_tick();
    auto_install_tick();
    if (state == ST_DONE && time_reached(reboot_at)) {
        printf("Reiniciando con la versión nueva...\n");
        sleep_ms(50);
        motor_set(0);
        leds_fill(0, 0, 0);
        rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE, 100, XIP_BASE + w.part_start, 0);
        while (true) tight_loop_contents();
    }
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!wifi_online()) return;
    if (!next_check) schedule_check(FIRST_CHECK_MS);
    if ((int32_t)(now - next_check) >= 0 && (state == ST_IDLE || state == ST_UP_TO_DATE ||
                                              state == ST_AVAILABLE || state == ST_ERROR)) {
        schedule_check(RETRY_MS);  // por si no llega a terminar
        ota_check();
    }
}

int ota_state_json(char *out, size_t max) {
    ota_state_t st = state;
    uint32_t pct = st == ST_DOWNLOADING || st == ST_UPLOADING
                   ? (w.total ? (uint32_t)((uint64_t)w.received * 100 / w.total) : 0) : 0;
    return snprintf(out, max, "{\"fw\":%d,\"st\":\"%s\",\"v\":%d,\"p\":%lu,\"err\":\"%s\",\"auto\":%s}",
                    FW_VERSION, state_names[st], latest, (unsigned long)pct,
                    st == ST_ERROR ? error_msg : "", settings_auto_update() ? "true" : "false");
}
