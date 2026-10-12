#include <stdio.h>
#include "pico/stdlib.h"
#include "leds.h"
#include "motor.h"
#include "wifi.h"
#include "control.h"
#include "web_server.h"
#include "buttons.h"
#include "settings.h"
#include "mode_switch.h"

#define BOOT_FLAG_MS  5000
#define BOOT_FLAG_BRI 160

static absolute_time_t boot_flag_until;

// Bandera de España fija: indica que la vitrina ha arrancado
static void show_boot_flag(void) {
    for (int i = 0; i < NUM_LEDS; i++) {
        if ((i * 3) / NUM_LEDS == 1) leds_set(i, BOOT_FLAG_BRI, BOOT_FLAG_BRI * 170 / 255, 0);
        else                         leds_set(i, BOOT_FLAG_BRI, 0, 0);
    }
    leds_show();
}

int main(void) {
    stdio_init_all();
    motor_init();
    leds_init();
    buttons_init();
    settings_init();

    // Nada más arrancar, 5 s de bandera con el motor parado; el WiFi conecta mientras
    boot_flag_until = make_timeout_time_ms(BOOT_FLAG_MS);
    show_boot_flag();

    wifi_mode_t mode = wifi_start();

    // Color si no hay modos guardados: verde = WiFi externa, morado = red propia, rojo = error
    if (mode == WIFI_MODE_STA)     { control.r = 0;   control.g = 255; control.b = 0; }
    else if (mode == WIFI_MODE_AP) { control.r = 255; control.g = 0;   control.b = 255; }
    else                           { control.r = 255; control.g = 0;   control.b = 0; }

    if (mode != WIFI_MODE_NONE) web_server_start(mode == WIFI_MODE_AP);

    while (!time_reached(boot_flag_until)) {
        wifi_tick();
        sleep_ms(10);
    }
    control_start_preset();  // siempre empieza en el modo 1

    uint32_t last_log = 0;
    while (true) {
        control_tick();
        wifi_tick();
        buttons_tick();
        mode_switch_tick();

        if (buttons_take_click(0)) {
            control_next_preset();
            printf("Modo de LED -> %d%s\n", control.preset + 1, control.preset == PRESET_OFF ? " (apagado)" : "");
        }
        if (buttons_take_click(1)) {
            control_motor_stair();
            printf("Motor -> %d%%\n", control.motor);
        }
        if (!settings_save_if_dirty()) printf("ERROR guardando ajustes en flash\n");
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_log >= 10000) {
            last_log = now;
            printf("modo=%s ip=%s\n",
                   mode == WIFI_MODE_STA ? (wifi_connected() ? settings_wifi_ssid() : "conectando")
                   : mode == WIFI_MODE_AP ? "red propia" : "error",
                   wifi_ip());
        }
        sleep_ms(1);
    }
}
