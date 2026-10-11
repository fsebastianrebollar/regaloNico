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

#define BRI 40

// Mientras conecta: un punto ámbar que recorre la tira
static void connecting_anim(void) {
    static uint32_t last, pos;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last < 150) return;
    last = now;
    for (int i = 0; i < NUM_LEDS; i++) leds_set(i, 0, 0, 0);
    leds_set(pos++ % NUM_LEDS, BRI, BRI / 2, 0);
    leds_show();
}

int main(void) {
    stdio_init_all();
    motor_init();
    leds_init();
    buttons_init();
    settings_init();

    wifi_mode_t mode = wifi_start(settings_wifi_policy(), connecting_anim);

    // Color si no hay modos guardados: verde = WiFi de casa, morado = red propia, rojo = error
    if (mode == WIFI_MODE_STA)     { control.r = 0;   control.g = 255; control.b = 0; }
    else if (mode == WIFI_MODE_AP) { control.r = 255; control.g = 0;   control.b = 255; }
    else                           { control.r = 255; control.g = 0;   control.b = 0; }

    if (mode != WIFI_MODE_NONE) web_server_start(mode == WIFI_MODE_AP);

    control_start_preset();  // siempre empieza en el modo 1

    uint32_t last_log = 0;
    while (true) {
        control_tick();
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
                   mode == WIFI_MODE_STA ? "casa" : mode == WIFI_MODE_AP ? "red propia" : "error",
                   wifi_ip());
        }
        sleep_ms(1);
    }
}
