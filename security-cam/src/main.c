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
// ============================ SYSTEM ============================
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
// ============================= WIFI =============================
#include <esp_wifi.h>
// ============================= HTTP =============================
#include <esp_http_server.h>
// ============================ CAMERA ============================
#include "esp_camera.h"
// ================================================================


// ============================ SYSTEM ============================
#define DEVICE          "[ESP32 CAM]"
// ============================= WIFI =============================
#define WIFI_SSID       "ESP32-Cam AP"
#define WIFI_CHAN       7
#define WIFI_MSCO       4
// ============================ CAMERA ============================
/**
 * @brief Camera function - take picture and save on SPIFFS
 */
esp_err_t take_picture();

#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1              //software reset will be performed
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

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

/**
 * Latest photo store 
 */
size_t taken_photo_len = 0;
size_t taken_photo_width = 0;
size_t taken_photo_height = 0;
pixformat_t taken_photo_format = PIXFORMAT_JPEG;
uint8_t *taken_photo_buf = NULL; 
// ================================================================


const TickType_t delay = 250 / portTICK_PERIOD_MS;              // 0.25s


// ============================= WIFI =============================
/**
 * @brief Handle WiFi connect & disconnect event
 * source: https://github.com/espressif/esp-idf/blob/42cce06704a24b01721cd34920f25b2e48b88c55/examples/wifi/getting_started/softAP/main/softap_example_main.c
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(DEVICE, "[WIFI] Station "MACSTR" joined {AID=%d}",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(DEVICE, "[WIFI] Station "MACSTR" left {AID=%d}",
                 MAC2STR(event->mac), event->aid);
    }
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
            .authmode = WIFI_AUTH_OPEN      // Do not require password
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &custom_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
// ================================================================

// ============================= HTTP =============================
/**
 * @brief Get Handler for Webserver - index.html
 * source: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html
 */
esp_err_t get_handler(httpd_req_t *req) {
    ESP_LOGI(DEVICE, "[HTTP] GET index.html");
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
    ESP_LOGI(DEVICE, "[HTTP] GET latest-photo");
    if (taken_photo_buf == NULL) {
        ESP_LOGE(DEVICE, "[HTTP] CMD latest-photo no picture found");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No picture found!");
        return ESP_FAIL;
    }
    ESP_LOGI(DEVICE, "[CAM] Sending photo");
    esp_err_t res = httpd_resp_set_type(req, "image/jpeg");
    if(res == ESP_OK){
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    }

    if(res == ESP_OK){
        if(taken_photo_format == PIXFORMAT_JPEG){
            res = httpd_resp_send(req, (const char *)taken_photo_buf, taken_photo_len);
        } else {
            ESP_LOGE(DEVICE, "[HTTP] CMD latest-photo failed to send");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send picture!");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

/**
 * @brief Get Handler for Webserver - pir-event - due to problem with POST signal for taking photo implemented as GET
 */
esp_err_t pir_handler(httpd_req_t *req) {
    ESP_LOGI(DEVICE, "[HTTP] GET take-photo");
    esp_err_t ret = take_picture();
    if (ret == ESP_OK) {
        ESP_LOGI(DEVICE, "[HTTP] CMD take-photo success");
        char msg_buffer[127];
        sprintf(msg_buffer, "Picture taken! (%zu x %zu, size: %zu bytes)", taken_photo_width, taken_photo_height, taken_photo_len);
        httpd_resp_send(req, msg_buffer, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    } else {
        ESP_LOGI(DEVICE, "[HTTP] CMD take-photo failure");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to take picture!");
        return ESP_FAIL;
    }
}

/**
 * @brief For HTTP TAKE PHOTO request
 * source: https://github.com/espressif/esp32-camera/blob/master/README.md
 */
typedef struct {
        httpd_req_t *req;
        size_t len;
} jpg_chunking_t;

/**
 * @brief For HTTP TAKE PHOTO request
 * source: https://github.com/espressif/esp32-camera/blob/master/README.md
 */
static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len){
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index){
        j->len = 0;
    }
    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK){
        return 0;
    }
    j->len += len;
    return len;
}

/**
 * @brief Get Handler for Webserver - take-photo - due to problem with POST signal for taking photo implemented as GET
 * source: https://github.com/espressif/esp32-camera/blob/master/README.md
 */
esp_err_t take_handler(httpd_req_t *req){
    ESP_LOGI(DEVICE, "[CAM] Taking photo");
    
    gpio_set_level(4, 1);
    vTaskDelay(delay);
    camera_fb_t *photo = esp_camera_fb_get();
    gpio_set_level(4, 0);
    if (!photo) {
        ESP_LOGE(DEVICE, "[CAM] Photo capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    ESP_LOGI(DEVICE, "[CAM] Sending photo");
    esp_err_t res = httpd_resp_set_type(req, "image/jpeg");
    if(res == ESP_OK){
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    }

    if(res == ESP_OK){
        if(photo->format == PIXFORMAT_JPEG){
            res = httpd_resp_send(req, (const char *)photo->buf, photo->len);
        } else {
            jpg_chunking_t jchunk = {req, 0};
            res = frame2jpg_cb(photo, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
            httpd_resp_send_chunk(req, NULL, 0);
        }
    }
    
    taken_photo_len = photo->len;
    taken_photo_width = photo->width;
    taken_photo_height = photo->height;

    ESP_LOGI(DEVICE, "[CAM] Photo taken! Its size was: %zu bytes\n", photo->len);
    esp_camera_fb_return(photo);
    return ESP_OK;
}

/**
 * @brief Webserver Start
 * source: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html
 */
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(DEVICE, "[HTTP] Server start success");
        ESP_LOGI(DEVICE, "[HTTP] Registering endpoints");
        // Register availiable paths and requests
        // MAIN PAGE - INDEX.HTML
        httpd_uri_t index_get = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = get_handler,
            .user_ctx = NULL
        };
        // GET LATEST PHOTO
        httpd_register_uri_handler(server, &index_get);
        httpd_uri_t latest_get = {
            .uri      = "/latest-photo.jpg",
            .method   = HTTP_GET,
            .handler  = img_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &latest_get);
        // PIR EVENT - TAKE PHOTO
        httpd_uri_t pir_post = {
            .uri      = "/pir",
            .method   = HTTP_GET,
            .handler  = pir_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &pir_post);
        // MANUAL REQUEST FOR PHOTO (NOT SAVED)
        httpd_uri_t take_post = {
            .uri      = "/take-photo",
            .method   = HTTP_GET,
            .handler  = take_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &take_post);
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
// ================================================================

// ============================ CAMERA ============================
/**
 * @brief Prepare camera for taking picture
 * source: https://github.com/espressif/esp32-camera/blob/master/examples/main/take_picture.c
 */
esp_err_t init_camera()
{   
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(DEVICE, "[CAM] Initialization failed");
        return err;
    }
    
    ESP_LOGI(DEVICE, "[CAM] Camera initialized");
    return ESP_OK;
}

/**
 * @brief Takes a picture and saves it to temp memory
 */
esp_err_t take_picture() {
    ESP_LOGI(DEVICE, "[CAM] Taking photo");
    
    gpio_set_level(4, 1);
    vTaskDelay(delay);
    camera_fb_t *photo = esp_camera_fb_get();
    gpio_set_level(4, 0);

    if (taken_photo_buf != NULL) {
        free(taken_photo_buf);
    }
    taken_photo_buf = malloc(photo->len);
    mempcpy(taken_photo_buf, photo->buf, photo->len);

    taken_photo_format = photo->format;
    taken_photo_len = photo->len;
    taken_photo_width = photo->width;
    taken_photo_height = photo->height;

    ESP_LOGI(DEVICE, "[CAM] Photo taken! Its size was: %zu bytes\n", photo->len);
    esp_camera_fb_return(photo);
    return ESP_OK;
}

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

    // Call WiFi AP setup & start
    wifi_config_ap();
    // Start Webserver
    httpd_handle_t server = start_webserver();
    // Initialize camera
    if (ESP_OK != init_camera()) {
        return;
    }
    // Init Flash LED
    gpio_pad_select_gpio(4);
    gpio_set_direction(4, GPIO_MODE_OUTPUT);
    gpio_set_level(4, 0);
    // ================================================================

    // ========================== EXECUTION ===========================
    // Infinite loop, waiting for incoming request on web server
    while(1) {}
    // Unreachable due to infinite loop
    stop_webserver(server);
    // ================================================================
}
// ================================================================