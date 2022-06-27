#include <stdio.h>

#include "sdkconfig.h"

#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/twai.h"

#include "freertos/FreeRTOS.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define LOG_TAG "candump"

static SemaphoreHandle_t twai_run_rx_task; /** semaphore for starting and joining rx task */

static void twai_rx_task(void *arg); /** rx task entry function */

/**
 * application main entrypoint
 */
void app_main(void)
{
    // create rx task and signal semaphore
    twai_run_rx_task = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(
        /* pvTaskCode    */ twai_rx_task,
        /* pcName        */ "TWAI_RX",
        /* usStackDepth  */ 4096,
        /* pvParameters  */ NULL,
        /* uxPriority    */ configMAX_PRIORITIES - 1,
        /* pvCreatedTask */ NULL,
        /* xCoreID       */ tskNO_AFFINITY
    );

    // set gpio settings and level if rx led flashing configured
#if CONFIG_CAN_RX_LED
    gpio_config_t gpio_conf =
    {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << CONFIG_CAN_RX_LED_GPIO,
        .pull_down_en = 0,
        .pull_up_en   = 0,
    };
    gpio_config(&gpio_conf);

    // turn off led
#if CONFIG_CAN_RX_LED_LEVEL_ACTIVE_LOW
    // active low -> set high to turn off
    gpio_set_level(CONFIG_CAN_RX_LED_GPIO, 1);
#elif CONFIG_CAN_RX_LED_LEVEL_ACTIVE_HIGH
    // active high -> set low to turn off
    gpio_set_level(CONFIG_CAN_RX_LED_GPIO, 0);
#endif
#endif

    // configure twai driver pins
    ESP_LOGI(LOG_TAG, "RX GPIO: %d", CONFIG_CAN_XCVR_RX_GPIO);
    ESP_LOGI(LOG_TAG, "TX GPIO: %d", CONFIG_CAN_XCVR_TX_GPIO);
    twai_general_config_t g_conf = TWAI_GENERAL_CONFIG_DEFAULT(
        CONFIG_CAN_XCVR_TX_GPIO, CONFIG_CAN_XCVR_RX_GPIO, TWAI_MODE_NORMAL);

    // configure twai driver baudrate
#if CONFIG_CAN_BAUDRATE_1_MBITS
    ESP_LOGI(LOG_TAG, "CAN baudrate: 1 Mbit/s");
    twai_timing_config_t t_conf = TWAI_TIMING_CONFIG_1MBITS();
#elif CONFIG_CAN_BAUDRATE_800_KBITS
    ESP_LOGI(LOG_TAG, "CAN baudrate: 800 kbit/s");
    twai_timing_config_t t_conf = TWAI_TIMING_CONFIG_800KBITS();
#elif CONFIG_CAN_BAUDRATE_500_KBITS
    ESP_LOGI(LOG_TAG, "CAN baudrate: 500 kbit/s");
    twai_timing_config_t t_conf = TWAI_TIMING_CONFIG_500KBITS();
#elif CONFIG_CAN_BAUDRATE == CONFIG_CAN_BAUDRATE_250_KBITS
    ESP_LOGI(LOG_TAG, "CAN baudrate: 250 kbit/s");
    twai_timing_config_t t_conf= TWAI_TIMING_CONFIG_250KBITS();
#elif CONFIG_CAN_BAUDRATE == CONFIG_CAN_BAUDRATE_125_KBITS
    ESP_LOGI(LOG_TAG, "CAN baudrate: 125 kbit/s");
    twai_timing_config_t t_conf = TWAI_TIMING_CONFIG_125KBITS();
#elif CONFIG_CAN_BAUDRATE == CONFIG_CAN_BAUDRATE_100_KBITS
    ESP_LOGI(LOG_TAG, "CAN baudrate: 100 kbit/s");
    twai_timing_config_t t_conf = TWAI_TIMING_CONFIG_100KBITS();
#elif CONFIG_CAN_BAUDRATE == CONFIG_CAN_BAUDRATE_50_KBITS
    ESP_LOGI(LOG_TAG, "CAN baudrate: 50 kbit/s");
    twai_timing_config_t t_conf = TWAI_TIMING_CONFIG_50KBITS();
#elif CONFIG_CAN_BAUDRATE == CONFIG_CAN_BAUDRATE_25_KBITS
    ESP_LOGI(LOG_TAG, "CAN baudrate: 25 kbit/s");
    twai_timing_config_t t_conf = TWAI_TIMING_CONFIG_25KBITS();
#endif

    // configure twai driver filter
    twai_filter_config_t f_conf = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // install and start twai driver
    ESP_ERROR_CHECK(twai_driver_install(&g_conf, &t_conf, &f_conf));
    ESP_LOGI(LOG_TAG, "TWAI driver succesfully installed");
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(LOG_TAG, "TWAI driver succesfully started");
    
    // signal start and wait for rx task to finish
    xSemaphoreGive(twai_run_rx_task);
    vTaskDelay(pdMS_TO_TICKS(100));
    xSemaphoreTake(twai_run_rx_task, portMAX_DELAY);

    // stop and uninstall twai driver
    ESP_ERROR_CHECK(twai_stop());
    ESP_LOGI(LOG_TAG, "TWAI driver succesfully stopped");
    ESP_ERROR_CHECK(twai_driver_uninstall());
    ESP_LOGI(LOG_TAG, "TWAI driver succesfully uninstalled");

    // cleanup
    vSemaphoreDelete(twai_run_rx_task);
}

static void twai_rx_task(void *arg)
{
    twai_message_t msg;
    esp_err_t err;

    // wait for start signal
    xSemaphoreTake(twai_run_rx_task, portMAX_DELAY);
    ESP_LOGI(LOG_TAG, "RX task started");

    while (true)
    {
        // receive message or timeout after 10 ms
        err = twai_receive(&msg, pdMS_TO_TICKS(10));

        // turn off led
#if CONFIG_CAN_RX_LED
#if CONFIG_CAN_RX_LED_LEVEL_ACTIVE_LOW
        // active low -> set high to turn off
        gpio_set_level(CONFIG_CAN_RX_LED_GPIO, 1);
#elif CONFIG_CAN_RX_LED_LEVEL_ACTIVE_HIGH
        // active high -> set low to turn off
        gpio_set_level(CONFIG_CAN_RX_LED_GPIO, 0);
#endif
#endif

        if (err == ESP_ERR_TIMEOUT)
        {
            // skip iteration if no message received
            continue;
        }
        else if (err != ESP_OK)
        {
            // should not happen
            ESP_LOGW(LOG_TAG, "TWAI receive error %d", err);
            continue;
        }

        // turn on led
#if CONFIG_CAN_RX_LED
#if CONFIG_CAN_RX_LED_LEVEL_ACTIVE_LOW
        // active low -> set low to turn on
        gpio_set_level(CONFIG_CAN_RX_LED_GPIO, 0);
#elif CONFIG_CAN_RX_LED_LEVEL_ACTIVE_HIGH
        // active high -> set high to turn on
        gpio_set_level(CONFIG_CAN_RX_LED_GPIO, 1);
#endif
#endif

        // print message similar to how candump on linux looks like
        printf("twai %8x [%d]", msg.identifier, msg.data_length_code);
        for (int i = 0; i < msg.data_length_code; i++)
        {
            printf(" %02x", msg.data[i]);
        }
        printf("\n");
        fflush(stdout);
    }

    // signal finish and cleanup
    xSemaphoreGive(twai_run_rx_task);
    vTaskDelete(NULL);
}
