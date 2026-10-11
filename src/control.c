#include "control.h"
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "leds.h"
#include "motor.h"
#include "settings.h"

volatile control_state_t control = { .r = 255, .g = 0, .b = 255, .brightness = 60, .effect = FX_SOLID, .motor = 0, .preset = -1 };

static const char *const effect_names[FX_COUNT] = { "off", "solid", "rainbow", "chase", "breathe", "heartbeat", "disco", "spain" };

#define LED_FRAME_MS   20
#define MOTOR_STEP_MS  20
#define MOTOR_STEP_PCT 2

const char *control_effect_name(led_effect_t fx) {
    return fx < FX_COUNT ? effect_names[fx] : "off";
}

int control_effect_from_name(const char *name) {
    for (int i = 0; i < FX_COUNT; i++)
        if (!strcmp(name, effect_names[i])) return i;
    return -1;
}

// Escalera del botón 2: sube hasta arriba, baja hasta 0 y sigue al otro lado
// (sentido contrario), y vuelta a empezar
static const int motor_steps[] = { 0, 20, 33, 50, 33, 20, 0, -20, -33, -50, -33, -20 };
#define NUM_MOTOR_STEPS (int)(sizeof motor_steps / sizeof motor_steps[0])

bool control_apply_preset(int i) {
    if (i == PRESET_OFF) {
        control.effect = FX_OFF;
        control.preset = PRESET_OFF;
        return true;
    }
    const led_preset_t *p = settings_preset(i);
    if (!p) return false;
    control.r = p->r; control.g = p->g; control.b = p->b;
    control.effect = p->fx < FX_COUNT ? p->fx : FX_SOLID;
    control.preset = i;
    return true;
}

// Ciclo: modos guardados (saltando huecos vacíos) -> apagado -> modo 1...
void control_next_preset(void) {
    int from = control.preset;
    for (int k = 1; k <= NUM_PRESETS + 1; k++) {
        int i = (from + k + NUM_PRESETS + 1) % (NUM_PRESETS + 1);
        if (i == PRESET_OFF && from == PRESET_OFF) {
            // Sin ningún modo guardado: alterna entre apagado y luz fija
            control.effect = FX_SOLID;
            control.preset = -1;
            return;
        }
        if (control_apply_preset(i)) return;
    }
}

void control_start_preset(void) {
    for (int i = 0; i < NUM_PRESETS; i++)
        if (control_apply_preset(i)) return;
}

void control_motor_stair(void) {
    control.motor_step = (control.motor_step + 1) % NUM_MOTOR_STEPS;
    control.motor = motor_steps[control.motor_step];
}

static struct { bool on; uint8_t level, r, g, b; } overlay;

void control_overlay(uint8_t level, uint8_t r, uint8_t g, uint8_t b) {
    overlay.level = level; overlay.r = r; overlay.g = g; overlay.b = b;
    overlay.on = true;
}

void control_overlay_off(void) { overlay.on = false; }

static uint8_t scale(uint8_t v, uint32_t s) { return (uint8_t)(v * s / 255); }

static void wheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (pos < 85)       { *r = 255 - pos * 3; *g = pos * 3;       *b = 0; }
    else if (pos < 170) { pos -= 85;  *r = 0; *g = 255 - pos * 3; *b = pos * 3; }
    else                { pos -= 170; *r = pos * 3; *g = 0;       *b = 255 - pos * 3; }
}

// Pseudoaleatorio rápido y determinista (para la discoteca)
static uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

// Intensidad 0..255 de un pulso que sube en 40 ms y cae en 220 ms
static uint32_t pulse(int32_t dt) {
    if (dt < 0) return 0;
    if (dt < 40) return dt * 255 / 40;
    if (dt < 260) { uint32_t k = (260 - dt) * 255 / 220; return k * k / 255; }
    return 0;
}

// Latido "pum-pum" a ~70 ppm: golpe fuerte y otro más suave 250 ms después
static uint32_t heartbeat(uint32_t t_ms) {
    int32_t ph = t_ms % 860;
    uint32_t a = pulse(ph), b = pulse(ph - 250) * 6 / 10;
    uint32_t k = a > b ? a : b;
    return k < 8 ? 8 : k;  // brillo mínimo para que no se apague del todo
}

static void render_overlay(void) {
    // Rellena de forma proporcional; el último LED encendido, a medias
    uint32_t fill = (uint32_t)overlay.level * NUM_LEDS;  // en 1/255 de LED
    for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t part = fill >= (uint32_t)(i + 1) * 255 ? 255 : fill > (uint32_t)i * 255 ? fill - i * 255 : 0;
        leds_set(i, scale(overlay.r, part), scale(overlay.g, part), scale(overlay.b, part));
    }
    leds_show();
}

static void render_leds(uint32_t t_ms) {
    if (overlay.on) {
        render_overlay();
        return;
    }
    control_state_t s = control;
    uint32_t bri = s.brightness;
    for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t r = 0, g = 0, b = 0;
        switch (s.effect) {
        case FX_SOLID:
            r = s.r; g = s.g; b = s.b;
            break;
        case FX_RAINBOW:
            wheel((uint8_t)(t_ms / 10 + i * 256 / NUM_LEDS), &r, &g, &b);
            break;
        case FX_CHASE: {
            int head = (t_ms / 150) % NUM_LEDS;
            int d = (head - i + NUM_LEDS) % NUM_LEDS;  // distancia detrás de la cabeza
            uint32_t k = d == 0 ? 255 : d == 1 ? 80 : d == 2 ? 20 : 0;
            r = scale(s.r, k); g = scale(s.g, k); b = scale(s.b, k);
            break;
        }
        case FX_BREATHE: {
            uint32_t ph = (t_ms / 8) % 512;
            uint32_t k = ph < 256 ? ph : 511 - ph;
            k = 5 + k * k * 250 / (255 * 255);  // curva suave de 5 a 255 (sin pasarse de 255)
            r = scale(s.r, k); g = scale(s.g, k); b = scale(s.b, k);
            break;
        }
        case FX_DISCO: {
            // Cada 70 ms colores saturados al azar por LED; de vez en cuando, flash blanco
            uint32_t frame = t_ms / 70;
            if (hash32(frame) % 7 == 0) { r = g = b = 255; break; }
            uint32_t h = hash32(frame * 31 + i);
            if (h % 5 == 0) break;  // algún LED apagado para que "salte" más
            wheel((uint8_t)h, &r, &g, &b);
            break;
        }
        case FX_SPAIN:
            // Bandera: primer tercio rojo, el del medio amarillo, el último rojo
            if ((i * 3) / NUM_LEDS == 1) { r = 255; g = 170; b = 0; }
            else                         { r = 255; g = 0;   b = 0; }
            break;
        case FX_HEARTBEAT: {
            uint32_t k = heartbeat(t_ms);
            r = scale(s.r, k); g = scale(s.g, k); b = scale(s.b, k);
            break;
        }
        default:
            break;
        }
        leds_set(i, scale(r, bri), scale(g, bri), scale(b, bri));
    }
    leds_show();
}

static struct {
    bool on;
    absolute_time_t until;
    control_state_t saved;
} crazy;

void control_crazy_start(uint32_t ms) {
    if (!crazy.on) crazy.saved = control;  // si ya estaba activo, solo se alarga
    crazy.on = true;
    crazy.until = make_timeout_time_ms(ms);
    control.effect = FX_DISCO;
    control.brightness = 255;
    control.motor = 100;
}

uint32_t control_crazy_left_ms(void) {
    if (!crazy.on) return 0;
    int64_t us = absolute_time_diff_us(get_absolute_time(), crazy.until);
    return us > 0 ? (uint32_t)(us / 1000) : 0;
}

void control_stop_all(void) {
    crazy.on = false;
    control.motor = 0;
    control.motor_step = 0;  // el botón 2 vuelve a empezar desde 0
    control_apply_preset(PRESET_OFF);  // el botón 1 sigue en el modo 1
}

static void crazy_tick(void) {
    if (!crazy.on || !time_reached(crazy.until)) return;
    crazy.on = false;
    control_state_t s = crazy.saved;
    control.r = s.r; control.g = s.g; control.b = s.b;
    control.effect = s.effect;
    control.brightness = s.brightness;
    control.motor = s.motor;
    control.preset = s.preset;
}

void control_tick(void) {
    static uint32_t last_led, last_motor;
    static int motor_now;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    crazy_tick();

    if (now - last_led >= LED_FRAME_MS) {
        last_led = now;
        render_leds(now);
    }
    if (now - last_motor >= MOTOR_STEP_MS) {
        last_motor = now;
        int target = control.motor;
        if (motor_now < target)      motor_now = MIN(motor_now + MOTOR_STEP_PCT, target);
        else if (motor_now > target) motor_now = MAX(motor_now - MOTOR_STEP_PCT, target);
        motor_set(motor_now);
    }
}
