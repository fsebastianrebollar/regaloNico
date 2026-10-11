#pragma once

// DRV8835 en modo PHASE/ENABLE
#define MOT_PHASE 18
#define MOT_EN    19   // PWM
#define MOT_MODE  20

void motor_init(void);
void motor_set(int pct);  // -100..100, signo = sentido
