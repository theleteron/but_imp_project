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
// SYSTEM
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
// GPIO pins
#include <driver/gpio.h>
// WIFI
#include "esp_wifi.h"
// HTTP requests
#include "esp_tls.h"
#include "esp_http_client.h"


#define DEVICE          "[ESP32 PIR]"

#define LED_GPIO        GPIO_NUM_2                              // GPIO pin number for sending OUTPUT signal to LED
#define PIR_GPIO        GPIO_NUM_39                             // GPIO pin number for receiveing INPUT signal from PIR

#define WIFI_SSID       "ESP32-Cam AP"

#define DEFAULT_GW      "192.168.4.1"
#define DEFAULT_PORT    "80"
esp_ip4_addr_t my_ip;
esp_ip4_addr_t gateway;

const TickType_t delay = 250 / portTICK_PERIOD_MS;              // 0.25s

// ============================= WIFI =============================
/**
 * @brief Handle WiFi connect & disconnect event
 * source: https://github.com/espressif/esp-idf/blob/42cce06704a24b01721cd34920f25b2e48b88c55/examples/wifi/getting_started/softAP/main/softap_example_main.c
 * source: https://github.com/espressif/esp-idf/blob/5c33570524118873f7bd32490c7a0442fede4bf8/examples/wifi/fast_scan/main/fast_scan.c
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {                 // Connect on power ON
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {   // Attempt reconnect
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        my_ip = event->ip_info.ip;
        gateway = event->ip_info.gw;
        ESP_LOGI(DEVICE, "got ip:" IPSTR, IP2STR(&my_ip));
        ESP_LOGI(DEVICE, "got gw:" IPSTR, IP2STR(&gateway));
    }
}

/**
 * @brief Configure and start WiFi STA
 * source: https://github.com/espressif/esp-idf/blob/42cce06704a24b01721cd34920f25b2e48b88c55/examples/wifi/getting_started/softAP/main/softap_example_main.c
 * source: https://github.com/espressif/esp-idf/blob/5c33570524118873f7bd32490c7a0442fede4bf8/examples/wifi/fast_scan/main/fast_scan.c
 */
void wifi_config_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize default station as network interface instance (esp-netif)
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t custom_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -127,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &custom_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
// ================================================================

// ============================= HTTP =============================
/**
 * @brief Handle HTTP event
 * source: https://github.com/espressif/esp-idf/blob/5c33570524118873f7bd32490c7a0442fede4bf8/examples/protocols/esp_http_client/main/esp_http_client_example.c
 */
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(DEVICE, "[HTTP] ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(DEVICE, "[HTTP] CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(DEVICE, "[HTTP] HEADER SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(DEVICE, "[HTTP] HEADER {key=%s : value=%s}", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(DEVICE, "[HTTP] DATA {len=%d}", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(DEVICE, "[HTTP] FINISHED");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(DEVICE, "[HTTP] DISCONNECTED");
            break;
    }
    return ESP_OK;
}

/**
 * @brief Configure HTTP & send request
 * source: https://github.com/espressif/esp-idf/blob/5c33570524118873f7bd32490c7a0442fede4bf8/examples/protocols/esp_http_client/main/esp_http_client_example.c
 * source: https://github.com/espressif/esp-idf/blob/5c33570524118873f7bd32490c7a0442fede4bf8/examples/protocols/http_request/main/http_request_example_main.c
 */
void send_request_to_camera() {
    char local_response_buffer[2048] = {0};
    char temp_url[50];
    sprintf(temp_url, "http://%d.%d.%d.%d/take-photo", IP2STR(&gateway));
    esp_http_client_config_t config = {
        .url = temp_url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = local_response_buffer,        // Pass address of local buffer to get response
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(DEVICE, "[HTTP] Response from CAM to PIR signal : %d : %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(DEVICE, "[HTTP] Sending PIR signal to CAM failed {%s}", 
                esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}
// ================================================================

// ============================ MAIN ==============================
void app_main() {
    // ====================== CONFIGURATION PART ======================
    // Initialize NVS
    // Taken from https://github.com/espressif/esp-idf/blob/42cce06704a24b01721cd34920f25b2e48b88c55/examples/wifi/getting_started/softAP/main/softap_example_main.c
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // Configure WIFI module & connect
    wifi_config_sta();
    // Setup OUTPUT
    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);
    // Setup INPUT
    gpio_pad_select_gpio(PIR_GPIO);
    gpio_set_direction(PIR_GPIO, GPIO_MODE_INPUT);
    // ================================================================
    
    // ======================== LOGGING  SETUP ========================
    // Info printout variables
    int64_t timestamp;
    // ================================================================

    
    // ========================== EXECUTION ===========================
    // Infinite loop for detecting and deciding if signal should be sent to camera
    while(1) {  
        if (gpio_get_level(PIR_GPIO)) {
            timestamp = esp_timer_get_time();
            ESP_LOGI(DEVICE, "Motion detected at %lli!", timestamp);
            ESP_LOGI(DEVICE, "Sending signal to the camera!");
            send_request_to_camera();
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(delay);
            gpio_set_level(LED_GPIO, 0);
        } else {
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(delay);
        }
    }
    // ================================================================
}
// ================================================================