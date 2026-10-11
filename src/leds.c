// Tira WS2815 (protocolo WS2812, GRB a 800 kHz) mediante PIO.
#include "leds.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

static PIO pio = pio0;
static uint sm = 0;
static uint32_t buf[NUM_LEDS];

void leds_init(void) {
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, LED_PIN, 800000);
    leds_fill(0, 0, 0);
}

void leds_set(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i >= 0 && i < NUM_LEDS) buf[i] = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
}

void leds_show(void) {
    for (int i = 0; i < NUM_LEDS; i++) pio_sm_put_blocking(pio, sm, buf[i] << 8u);
    sleep_us(300);  // latch (>280 us)
}

void leds_fill(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < NUM_LEDS; i++) leds_set(i, r, g, b);
    leds_show();
}
