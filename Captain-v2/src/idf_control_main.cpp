#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

extern "C" void app_main(void) {
    static const char* TAG = "captain_control_idf";
    ESP_LOGI(TAG, "Captain control ESP-IDF scaffold started");

    uint32_t tick = 0;
    while (true) {
        if ((tick % 200) == 0) {
            ESP_LOGI(TAG, "alive tick=%lu", static_cast<unsigned long>(tick));
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
