#include "motor.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#define PWM_WRAP 7499  // 150 MHz / 7500 = 20 kHz

void motor_init(void) {
    gpio_init(MOT_MODE);  gpio_set_dir(MOT_MODE, GPIO_OUT);  gpio_put(MOT_MODE, 1);
    gpio_init(MOT_PHASE); gpio_set_dir(MOT_PHASE, GPIO_OUT); gpio_put(MOT_PHASE, 0);
    gpio_set_function(MOT_EN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(MOT_EN);
    pwm_set_wrap(slice, PWM_WRAP);
    pwm_set_gpio_level(MOT_EN, 0);
    pwm_set_enabled(slice, true);
}

void motor_set(int pct) {
    if (pct > 100) pct = 100;
    if (pct < -100) pct = -100;
    gpio_put(MOT_PHASE, pct < 0);
    if (pct < 0) pct = -pct;
    pwm_set_gpio_level(MOT_EN, (uint32_t)pct * (PWM_WRAP + 1) / 100);
}
