#pragma once
#include <stdint.h>

#define LED_PIN  16
#define NUM_LEDS 6

void leds_init(void);
void leds_fill(uint8_t r, uint8_t g, uint8_t b);
void leds_set(int i, uint8_t r, uint8_t g, uint8_t b);
void leds_show(void);
