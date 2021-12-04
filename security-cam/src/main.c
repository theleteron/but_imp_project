/**
 * @file main.c
 * @author Roman Janiczek (xjanic25@vutbr.cz)
 * @brief Camera capture & Web server & AP point
 * @version 0.1
 * @date 2021-11-30
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
// WiFi
#include <esp_wifi.h>
// WebServer
#include <esp_http_server.h>
// SDCard
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_defs.h>
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_vfs.h"
// GPIO pins
#include <driver/gpio.h>
// Camera
#include "esp_camera.h"

#define WIFI_SSID "ESP32-Cam AP"
#define WIFI_CHAN 7
#define WIFI_MSCO 4

#define LED_GPIO GPIO_NUM_4                                     // GPIO pin number for sending OUTPUT signal to LED (FLASH)
#define PIR_GPIO GPIO_NUM_16                                    // GPIO pin number for receiveing INPUT signal from PIR

#define MOUNTPOINT "/sdcard"

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1 //software reset will be performed
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#define SCRATCH_BUFSIZE  8192
struct file_server_data {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
};

const TickType_t delay = 250 / portTICK_PERIOD_MS;              // 5s

/**
 * @brief Handle WiFi connect & disconnect event
 * source: https://github.com/espressif/esp-idf/blob/42cce06704a24b01721cd34920f25b2e48b88c55/examples/wifi/getting_started/softAP/main/softap_example_main.c
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{

}

/**
 * @brief Configure and start WiFi AP
 * source: https://github.com/espressif/esp-idf/blob/42cce06704a24b01721cd34920f25b2e48b88c55/examples/wifi/getting_started/softAP/main/softap_example_main.c
 */
void wifi_config_ap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t custom_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHAN,
            .max_connection = WIFI_MSCO,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &custom_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 * @brief Get Handler for Webserver - index.html
 * source: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html
 */
esp_err_t get_handler(httpd_req_t *req) {
    const char resp[] = "\\
    <!DOCTYPE HTML>\\
    <html>\\
        <head>\\
            <title>ESP32-Cam WebInterface</title>\\
            <meta name='viewport' content='width=device-width, initial-scale=1'>\\
            <style>\\
                .main {\\
                    margin: auto;\\
                    width: 75%;\\
                }\\
                .title {\\
                    text-align: center;\\
                    display: flex;\\
                    justify-content: center;\\
                }\\
                .camera {\\
                    display: flex;\\
                    justify-content: center;\\
                }\\
                .camera image {\\
                    width: 100%;\\
                }\\
                .refresh {\\
                    padding-top: 2rem;\\
                    display: flex;\\
                    justify-content: center;\\
                }\\
            </style>\\
            <script>\\
                function refreshPhoto() {\\
                    var source = 'latest-photo.jpg',\\
                    timestamp = (new Date()).getTime(),\\
                    newUrl = source + '?_=' + timestamp;\\
                    document.getElementById('photo').src = newUrl;\\
                }\\
                setInterval(refreshPhoto, 5000);\\
            </script>\\
        </head>\\
        <body>\\
            <div class='main'>\\
                <div class='title'>\\
                    <h1>Latest photo captured by ESP32 Camera</h1>\\
                </div>\\
                <div class='camera'>\\
                    <image id='photo' src='latest-photo.jpg' width='75%' />\\
                </div>\\
                <div class='refresh'>\\
                    <button onclick='refreshPhoto();'>Refresh (auto every 5s)</button>\\
                </div>\\
            </div>\\
        </body>\\
    </html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Get Handler for Webserver - latest-photo - gets latest photo taken
 * source: https://github.com/espressif/esp-idf/blob/master/examples/protocols/http_server/file_serving/main/file_server.c
 */
esp_err_t img_handler(httpd_req_t *req) {
    FILE *file = fopen(MOUNTPOINT"/latest-photo.jpg", "r");
    if (file != NULL) {
        httpd_resp_set_type(req, "image/jpg");
        char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
        size_t chunksize;
        do {
            chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, file);

            if (chunksize > 0) {
                if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                    fclose(file);
                    httpd_resp_sendstr_chunk(req, NULL);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
            }

            /* Keep looping till the whole file is sent */
        } while (chunksize != 0);
        fclose(file);
        return ESP_OK;
    }
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read file");
    return ESP_FAIL;
}

/**
 * @brief Uri Handler for Webserver
 * source: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html
 */
httpd_uri_t uri_get = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = get_handler,
    .user_ctx = NULL
};

/**
 * @brief Uri Handler for Webserver
 * source: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html
 */
httpd_uri_t img_get = {
    .uri      = "/latest-photo",
    .method   = HTTP_GET,
    .handler  = img_handler,
    .user_ctx = NULL
};

/**
 * @brief Webserver Start
 * source: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html
 */
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register availiable paths and requests
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &img_get);
    }

    return server;
}

/**
 * @brief Webserver stop
 * source: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html
 */
void stop_webserver(httpd_handle_t server) {
    if (server) {
        httpd_stop(server);
    }
}

/**
 * @brief Prepare SDCARD & mount
 * source: https://github.com/raphaelbs/esp32-cam-ai-thinker/blob/master/examples/sd_jpg/src/main.c
 */
static esp_err_t init_sdcard() {
    esp_err_t ret = ESP_FAIL;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 3,
    };
    sdmmc_card_t *card;
    ret = esp_vfs_fat_sdmmc_mount(MOUNTPOINT, &host, &slot_config, &mount_config, &card);
    return ret;
}

/**
 * @brief Prepare camera for taking picture
 * source: https://github.com/espressif/esp32-camera/blob/master/examples/main/take_picture.c
 */
static esp_err_t init_camera()
{
    static camera_config_t camera_config = {
        .pin_pwdn =     CAM_PIN_PWDN,
        .pin_reset =    CAM_PIN_RESET,
        .pin_xclk =     CAM_PIN_XCLK,
        .pin_sscb_sda = CAM_PIN_SIOD,
        .pin_sscb_scl = CAM_PIN_SIOC,

        .pin_d7 =       CAM_PIN_D7,
        .pin_d6 =       CAM_PIN_D6,
        .pin_d5 =       CAM_PIN_D5,
        .pin_d4 =       CAM_PIN_D4,
        .pin_d3 =       CAM_PIN_D3,
        .pin_d2 =       CAM_PIN_D2,
        .pin_d1 =       CAM_PIN_D1,
        .pin_d0 =       CAM_PIN_D0,
        .pin_vsync =    CAM_PIN_VSYNC,
        .pin_href =     CAM_PIN_HREF,
        .pin_pclk =     CAM_PIN_PCLK,

        //XCLK 20MHz or 10MHz
        .xclk_freq_hz = 20000000,
        .ledc_timer =   LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,     //YUV422,GRAYSCALE,RGB565,JPEG
        .frame_size =   FRAMESIZE_UXGA,     //QQVGA-UXGA Do not use sizes above QVGA when not JPEG

        .jpeg_quality = 12, //0-63 lower number means higher quality
        .fb_count = 1       //if more than one, i2s runs in continuous mode. Use only with JPEG
    };
    
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        return err;
    }

    return ESP_OK;
}

/**
 * @brief Takes a picture and saves it to the SD card
 * 
 * @return esp_err_t 
 */
esp_err_t take_picture() {
    camera_fb_t *photo = esp_camera_fb_get();
    /*int64_t timestamp = esp_timer_get_time();
    char *photo_name = malloc(17 + sizeof(int64_t));
    char *latest_name = MOUNTPOINT"/latest-photo.jpg";
    sprintf(photo_name, MOUNTPOINT"/photo_%lli.jpg", timestamp);
    FILE *file = fopen(photo_name, "w");
    if (file != NULL) {
        fwrite(photo->buf, 1, photo->len, file);
    }
    fclose(file);
    file = fopen(latest_name, "w");
    if (file != NULL) {
        fwrite(photo->buf, 1, photo->len, file);
    }
    fclose(file);
    free(photo_name);*/
    printf("Photo taken!\n");
    esp_camera_fb_return(photo);
    return ESP_OK;
}

void app_main() {
    // Initialize NVS
    // Taken from https://github.com/espressif/esp-idf/blob/42cce06704a24b01721cd34920f25b2e48b88c55/examples/wifi/getting_started/softAP/main/softap_example_main.c
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Call WiFi AP setup & start
    wifi_config_ap();
    // Start Webserver
    httpd_handle_t server = start_webserver();
    // Initialize sdcard
    /*if (ESP_OK != init_sdcard()) {
        return;
    }*/
    // Initialize camera
    /*if (ESP_OK != init_camera()) {
        return;
    }*/
    // Setup OUTPUT
    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);
    // Setup INPUT
    gpio_pad_select_gpio(PIR_GPIO);
    gpio_set_direction(PIR_GPIO, GPIO_MODE_INPUT);
    // Infinite loop for detecting and deciding if signal should be sent to camera
    while(1) {
        if (gpio_get_level(PIR_GPIO)) {
            gpio_set_level(LED_GPIO, 1);    // Flash
            //take_picture();
            vTaskDelay(delay);
            gpio_set_level(LED_GPIO, 0);
        } else {
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(delay);
        }
    }
    // Unreachable due to infinite loop
    stop_webserver(server);
}