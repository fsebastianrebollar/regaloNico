#include "mode_switch.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "buttons.h"
#include "control.h"
#include "leds.h"
#include "motor.h"
#include "settings.h"

#define HOLD_MS    5000
#define BAR_BRI    80

static void blink_and_reboot(uint8_t r, uint8_t g, uint8_t b) {
    motor_set(0);
    for (int i = 0; i < 3; i++) {
        leds_fill(r, g, b);
        sleep_ms(250);
        leds_fill(0, 0, 0);
        sleep_ms(150);
    }
    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();
}

void mode_switch_tick(void) {
    static bool holding;
    static uint32_t start;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (!(buttons[0].pressed && buttons[1].pressed)) {
        if (holding) {
            holding = false;
            control_overlay_off();
            printf("Cambio de modo cancelado\n");
        }
        return;
    }
    if (!holding) {
        holding = true;
        start = now;
        printf("Botones mantenidos: cambio de modo en %d s\n", HOLD_MS / 1000);
    }

    uint32_t held = now - start;
    if (held < HOLD_MS) {
        control_overlay((uint8_t)(held * 255 / HOLD_MS), BAR_BRI, BAR_BRI, BAR_BRI);
        return;
    }

    wifi_policy_t next = settings_wifi_policy() == WIFI_POLICY_AUTO ? WIFI_POLICY_AP_ONLY : WIFI_POLICY_AUTO;
    settings_set_wifi_policy(next);
    bool ok = settings_save_if_dirty();
    printf("Modo WiFi -> %s (%s). Reiniciando...\n",
           next == WIFI_POLICY_AUTO ? "automatico" : "solo red propia", ok ? "guardado" : "ERROR al guardar");
    sleep_ms(50);  // que salga el mensaje por USB
    if (!ok) blink_and_reboot(BAR_BRI, 0, 0);
    if (next == WIFI_POLICY_AUTO) blink_and_reboot(0, BAR_BRI, 0);
    else                          blink_and_reboot(BAR_BRI, 0, BAR_BRI);
}
