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

#define LED_GPIO GPIO_NUM_2                                     // GPIO pin number for sending OUTPUT signal to LED
#define CAM_GPIO GPIO_NUM_14                                    // GPIO pin number for sending OUTPUT signal to CAMERA
#define PIR_GPIO GPIO_NUM_39                                    // GPIO pin number for receiveing INPUT signal from PIR

const TickType_t delay = 250 / portTICK_PERIOD_MS;              // 0.25s

void app_main() {
    // Setup OUTPUT
    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);
    gpio_pad_select_gpio(CAM_GPIO);
    gpio_set_direction(CAM_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CAM_GPIO, 0);
    // Setup INPUT
    gpio_pad_select_gpio(PIR_GPIO);
    gpio_set_direction(PIR_GPIO, GPIO_MODE_INPUT);
    // Infinite loop for detecting and deciding if signal should be sent to camera
    while(1) {  
        if (gpio_get_level(PIR_GPIO)) {
            printf("Motion detected!\n");
            printf("Sending signal to the camera!\n");
            gpio_set_level(LED_GPIO, 1);
            gpio_set_level(CAM_GPIO, 1);
            vTaskDelay(delay);
            gpio_set_level(LED_GPIO, 0);
            gpio_set_level(CAM_GPIO, 0);
        } else {
            printf("No motion detected!\n");
            gpio_set_level(LED_GPIO, 0);
            gpio_set_level(CAM_GPIO, 0);
            vTaskDelay(delay);
        }
    }
}