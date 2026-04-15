#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include <pico/time.h>
#include <stdint.h>

#define SERVO_PIN       15          // GP15 for servo signal
#define SERVO_MIN_US    600         // ~0°  (tune per servo)
#define SERVO_MAX_US    2400        // ~180° (tune per servo)
#define SERVO_FREQ_HZ   50          // 50 Hz -> 20 ms period

static uint slice_num;
static uint16_t top_value;

// Convert angle (0–180) to PWM level (0–top_value)
uint16_t angle_to_level(float angle_deg) {
    if (angle_deg < 0) angle_deg = 0;
    if (angle_deg > 180) angle_deg = 180;

    float pulse_us_min = SERVO_MIN_US;
    float pulse_us_max = SERVO_MAX_US;
    float pulse_us = pulse_us_min + (pulse_us_max - pulse_us_min) * (angle_deg / 180.0f);

    float period_us = 1000000.0f / SERVO_FREQ_HZ; // 20 000 us at 50 Hz

    // level / top = pulse_us / period_us
    float level = (pulse_us / period_us) * (top_value + 1);
    if (level < 0) level = 0;
    if (level > top_value) level = top_value;

    return (uint16_t)level;
}

int main() {
    stdio_init_all();

    // Set GPIO for PWM function
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(SERVO_PIN);

    // Compute divider and wrap so PWM clock is 1 MHz, then 1 MHz / 50 Hz = 20 000 counts
    uint32_t f_sys = clock_get_hz(clk_sys);        // ~125 MHz on Pico[web:25][web:37]
    float pwm_clk_hz = 1000000.0f;                 // 1 MHz PWM clock[web:25]
    float divider = (float)f_sys / pwm_clk_hz;     // pwm clock = f_sys / divider[web:25][web:28]
    pwm_set_clkdiv(slice_num, divider);

    top_value = (uint16_t)(pwm_clk_hz / SERVO_FREQ_HZ - 1); // 1e6 / 50 - 1 = 19999[web:25]
    pwm_set_wrap(slice_num, top_value);

    // Start with servo at 0°
    uint16_t level = angle_to_level(0.0f);
    pwm_set_gpio_level(SERVO_PIN, level);
    pwm_set_enabled(slice_num, true);

    while (true) {
        // Sweep 0 -> 180°
        for (int angle = 0; angle <= 180; angle += 10) {
            pwm_set_gpio_level(SERVO_PIN, angle_to_level((float)angle));
            sleep_ms(500);
        }

        // pwm_set_gpio_level(SERVO_PIN, angle_to_level(180.0f));

        sleep_ms(3000);

        // pwm_set_gpio_level(SERVO_PIN, angle_to_level(0.0f));

        // Sweep 180 -> 0°
        for (int angle = 180; angle >= 0; angle -= 10) {
            pwm_set_gpio_level(SERVO_PIN, angle_to_level((float)angle));
            sleep_ms(500);
        }

        sleep_ms(2000);
    }
}