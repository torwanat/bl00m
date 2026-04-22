#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include <pico/stdio.h>
#include <pico/time.h>
#include <stdint.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <string.h>

// For servo control
#define SERVO_PIN 15
#define SERVO_MIN_US 600
#define SERVO_MAX_US 2400
#define SERVO_FREQ_HZ 50

// Servo params
#define CLOSED_ANGLE 0.0f
#define OPENED_ANGLE 73.0f
#define STEP 5
#define PAUSE 300

// For LDR reading
#define LDR_PIN 26
#define ADC_INPUT 0
#define UPPER_THRESHOLD 1400
#define LOWER_THRESHOLD 1000

// For persistent storage
#define FLASH_TARGET_OFFSET  (256 * 1024)

// XIP_BASE is the start of flash in the address space
const uint8_t *flash_base = (const uint8_t *) XIP_BASE;
const uint8_t *flash_cfg  = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

// Object to be saved to storage 
//     magic: integrity check
//     angle: current angle of the servo
typedef struct {
    uint32_t magic;
    float    angle;
} config_t;

// Funny word for a magic he he
#define CONFIG_MAGIC 0xABADBEEF

// Program states
enum state {INITIAL, DARK, BRIGHT, MOVING};
enum state currentState = INITIAL;

// For PWM
static uint slice_num;
static uint16_t top_value;

// For reading LDR value
const float VREF = 3.3f;
const float conversion_factor = VREF / (1 << 12);

// Data saved in storage
const config_t *cfg_in_flash = (const config_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

// Current program data
config_t current_cfg;

// Try to load data from storage
void load_config(void) {
    // Integrity check
    if (cfg_in_flash->magic == CONFIG_MAGIC) {
        // Magic from storage matches the program magic, the data is intact
        current_cfg = *cfg_in_flash;
    } else {
        // Magics doesn't match, data corrupted -> set defaults

        // Set the servo to closed position
        pwm_set_gpio_level(SERVO_PIN, CLOSED_ANGLE);
        // Rebuild the config with default values
        current_cfg.magic = CONFIG_MAGIC;
        current_cfg.angle = CLOSED_ANGLE;
    }
}

// Save the data to storage
void save_config(void) {
    // Prepare a 256-byte page buffer
    uint8_t page_buf[FLASH_PAGE_SIZE];
    memset(page_buf, 0xFF, FLASH_PAGE_SIZE);
    memcpy(page_buf, &current_cfg, sizeof(current_cfg));

    // Critical section: no interrupts during erase/program
    uint32_t ints = save_and_disable_interrupts();

    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, page_buf, FLASH_PAGE_SIZE);

    restore_interrupts(ints);
}


// Convert angle (0–180) to PWM level (0–top_value) (stolen from internet)
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

// Move servo from one angle to another but in small increments
void moveServo(int16_t startAngle, int16_t endAngle, uint16_t step, uint16_t delay_ms) {
    // Compare the angles to find the direction of the move
    if (endAngle > startAngle) {
        // Move servo forward
        for (; startAngle <= endAngle; startAngle += step) {
            pwm_set_gpio_level(SERVO_PIN, angle_to_level((float)startAngle));
            sleep_ms(delay_ms);
        }
    } else {
        // Move servo backwards
        for (; endAngle <= startAngle; startAngle -= step) {
            pwm_set_gpio_level(SERVO_PIN, angle_to_level((float)startAngle));
            sleep_ms(delay_ms);
        }
    }
}

int main() {
    // Initialize everything, configure pins, PWM and all that
    stdio_init_all();

    adc_init();

    adc_gpio_init(LDR_PIN);
    adc_select_input(ADC_INPUT);

    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(SERVO_PIN);

    // Clock management for PWM (also stolen from internet)
    uint32_t f_sys = clock_get_hz(clk_sys);
    float pwm_clk_hz = 1000000.0f;
    float divider = (float)f_sys / pwm_clk_hz;
    pwm_set_clkdiv(slice_num, divider);

    top_value = (uint16_t)(pwm_clk_hz / SERVO_FREQ_HZ - 1);
    pwm_set_wrap(slice_num, top_value);

    pwm_set_enabled(slice_num, true);

    // Load the config from persistent storage
    load_config();

    // Set the correct state according to the data retrieved from the storage
    if (current_cfg.angle == CLOSED_ANGLE) {
        currentState = DARK;
    } else {
        currentState = BRIGHT;
    }

    while (true) {
        // Read the LDR value
        uint16_t raw = adc_read();

        // For debugging
        // float voltage = raw * conversion_factor;
        // printf("Raw: %u, Voltage: %.3f V, State: %u\n", raw, voltage, currentState);

        if (raw > UPPER_THRESHOLD && currentState != BRIGHT) {
            // It was dark but it's bright now -> open the flower
            currentState = MOVING;

            // Move servo from CLOSED_ANGLE to OPENED_ANGLE
            moveServo(CLOSED_ANGLE, OPENED_ANGLE, STEP, PAUSE);

            // Save the angle to the persistent storage
            current_cfg.angle = OPENED_ANGLE;
            save_config();

            currentState = BRIGHT;
        } else if (raw < LOWER_THRESHOLD && currentState != DARK) {
            // It was bright but it's dark now -> close the flower
            currentState = MOVING;

            // Move the servo from OPENED_ANGLE to CLOSED_ANGLE
            moveServo(OPENED_ANGLE, CLOSED_ANGLE, STEP, PAUSE);

            // Save the angle to the persistent storage
            current_cfg.angle = CLOSED_ANGLE;
            save_config();

            currentState = DARK;
        }

        sleep_ms(500);
    }
}