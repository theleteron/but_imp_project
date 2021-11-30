/**
 * @file main.c
 * @author Roman Janiczek (xjanic25@vutbr.cz)
 * @brief Motion detection sensor
 * @version 0.1
 * @date 2021-11-30
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#include <stdio.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define OUTPUT_GPIO GPIO_NUM_26                                  // GPIO pin number for sending OUTPUT signal to CAMERA
#define INPUT_GPIO GPIO_NUM_39                                   // GPIO pin number for receiveing INPUT signal from PIR

const TickType_t delay = 1000 / portTICK_PERIOD_MS;

void app_main() {
    // Setup OUTPUT
    gpio_pad_select_gpio(OUTPUT_GPIO);
    gpio_set_direction(OUTPUT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(OUTPUT_GPIO, 0);
    // Setup INPUT
    gpio_pad_select_gpio(INPUT_GPIO);
    gpio_set_direction(INPUT_GPIO, GPIO_MODE_INPUT);
    // Infinite loop for detecting and deciding if signal should be sent to camera
    while(1) {  
        if (gpio_get_level(INPUT_GPIO)) {
            printf("Motion detected!\n");
            printf("Sending signal to the camera!\n");
            gpio_set_level(OUTPUT_GPIO, 1);
            vTaskDelay(delay);
            gpio_set_level(OUTPUT_GPIO, 0);
        } else {
            printf("No motion detected!\n");
            gpio_set_level(OUTPUT_GPIO, 0);
            vTaskDelay(delay);
        }
    }
}