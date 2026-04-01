#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

extern "C" void app_main(void) {
    static const char* TAG = "captain_display_idf";
    ESP_LOGI(TAG, "Captain display ESP-IDF scaffold started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "display-link scaffold alive");
    }
}
