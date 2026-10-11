#pragma once
#include <stdbool.h>
#include <stdint.h>

// Botones a 3,3 V al pulsar (pull-down interno)
#define NUM_BUTTONS 2
#define BUTTON_PINS { 14, 15 }

typedef struct {
    bool pressed;
    uint32_t presses;  // pulsaciones desde el arranque
} button_state_t;

extern volatile button_state_t buttons[NUM_BUTTONS];

void buttons_init(void);
void buttons_tick(void);  // llamar a menudo desde el bucle principal

// true una vez por cada pulsación simple del botón i, al soltarlo. No cuenta
// si el otro botón se ha pulsado mientras tanto (combinación de los dos).
bool buttons_take_click(int i);

// Pulsación simulada (desde la web): misma acción que el botón físico
void buttons_inject_click(int i);
