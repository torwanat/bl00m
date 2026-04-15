#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

int main() {
    // Initialize stdio over USB (enable in CMakeLists with
    // pico_enable_stdio_usb(main 1) and pico_enable_stdio_uart(main 0))
    stdio_init_all();

    // Initialize the ADC
    adc_init();

    // Configure GPIO26 (ADC0) for analog input
    adc_gpio_init(26);

    // Select ADC input 0 (GPIO26)
    adc_select_input(0);

    const float VREF = 3.3f;
    const float conversion_factor = VREF / (1 << 12);  // 12-bit ADC: 0..4095

    while (true) {
        // Read raw value (0..4095)
        uint16_t raw = adc_read();

        // Convert to voltage
        float voltage = raw * conversion_factor;

        // Print raw and voltage
        printf("Raw: %u, Voltage: %.3f V\n", raw, voltage);

        // Small delay
        sleep_ms(200);
    }

    return 0;
}