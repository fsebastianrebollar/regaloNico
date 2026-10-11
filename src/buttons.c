#include "buttons.h"
#include <stdio.h>
#include "pico/stdlib.h"

#define SAMPLE_MS   5
#define DEBOUNCE_MS 30

volatile button_state_t buttons[NUM_BUTTONS];

static const uint pins[NUM_BUTTONS] = BUTTON_PINS;
static bool combo_seen[NUM_BUTTONS];  // el otro botón se pulsó durante esta pulsación
static volatile int pending_clicks[NUM_BUTTONS];  // también se suman desde la web

void buttons_init(void) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_down(pins[i]);
        // Errata RP2350-E9: con el buffer de entrada siempre activo, un pin con
        // pull-down puede quedarse enganchado en alto. Solo lo activamos al leer.
        gpio_set_input_enabled(pins[i], false);
    }
}

static bool read_pin(uint pin) {
    gpio_set_input_enabled(pin, true);
    busy_wait_us(2);
    bool v = gpio_get(pin);
    gpio_set_input_enabled(pin, false);
    return v;
}

void buttons_tick(void) {
    static uint32_t last_sample;
    static bool raw_prev[NUM_BUTTONS];
    static uint32_t stable_since[NUM_BUTTONS];
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_sample < SAMPLE_MS) return;
    last_sample = now;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool raw = read_pin(pins[i]);
        if (raw != raw_prev[i]) {
            raw_prev[i] = raw;
            stable_since[i] = now;
        } else if (raw != buttons[i].pressed && now - stable_since[i] >= DEBOUNCE_MS) {
            buttons[i].pressed = raw;
            if (raw) {
                buttons[i].presses++;
                combo_seen[i] = false;
            } else if (!combo_seen[i]) {
                pending_clicks[i]++;
            }
            printf("BOTON %d (GP%u) %s\n", i + 1, pins[i], raw ? "pulsado" : "soltado");
        }
    }
    if (buttons[0].pressed && buttons[1].pressed) combo_seen[0] = combo_seen[1] = true;
}

void buttons_inject_click(int i) {
    if (i < 0 || i >= NUM_BUTTONS) return;
    buttons[i].presses++;
    pending_clicks[i]++;
}

bool buttons_take_click(int i) {
    if (pending_clicks[i] == 0) return false;
    pending_clicks[i]--;
    return true;
}
