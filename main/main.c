#include "main.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "iopanel.h"
#include "wash_cycle.h"
#include "devstate.h"
#include "pressure_switch.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "esp_app_trace.h"
#include "esp_log.h"

pca9554_handle_t pca_u3_handle, pca_u4_handle, pca_u5_handle;
SemaphoreHandle_t i2c_mutex = NULL;

void app_main(void)
{
	esp_log_set_vprintf(esp_apptrace_vprintf);

	esp_event_loop_create_default();

	i2c_master_bus_config_t i2c_bus_config = {
		.i2c_port = -1,
		.sda_io_num = GPIO_NUM_3,
		.scl_io_num = GPIO_NUM_2,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
	};
	i2c_master_bus_handle_t i2c_bus_handle;
	i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);

	i2c_mutex = xSemaphoreCreateMutex();

	pca9554_config_t pca_config = {
		.i2c_bus_handle = i2c_bus_handle,
		.i2c_bus_mutex = i2c_mutex,
		.pin_a0 = 1,
		.pin_a1 = 1,
		.pin_a2 = 1,
		.shared = false,
	};
	pca9554A_init(&pca_config, &pca_u3_handle);

	pca_config.pin_a2 = 0;
	pca9554A_init(&pca_config, &pca_u5_handle);

	pca_config.pin_a0 = pca_config.pin_a1 = 0;
	pca9554A_init(&pca_config, &pca_u4_handle);

	pca9554_write_config(PCA_MOTOR_PUMP, 0xFF & ~(PCA_PIN_PUMP | PCA_PIN_MOTOR_CW | PCA_PIN_MOTOR_CCW), portMAX_DELAY); // set outputs
	pca9554_write_config(PCA_VALVES_PRES_SWITCH, 0xFF & ~(PCA_PIN_VALVE_1 | PCA_PIN_VALVE_2 | PCA_PIN_VALVE_3), portMAX_DELAY); // set outputs

	gpio_install_isr_service(0);

	TaskHandle_t iopanel_task;
	xTaskCreate(iopanel_task_entry, "IO Panel Loop", 1536, NULL, IOPANEL_TASK_PRIORITY, &iopanel_task);

	TaskHandle_t devstate_task;
	xTaskCreate(devstate_task_entry, "Device State Machine", 1536, NULL, DEVSTATE_TASK_PRIORITY, &devstate_task);

	TaskHandle_t pressure_switch_task;
	xTaskCreate(pressure_switch_task_entry, "Pressure Switch Check", 1536, NULL, PRESSURE_SWITCH_TASK_PRIORITY, &pressure_switch_task);

	{
		esp_err_t ret = nvs_flash_init();
		if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
			/* NVS partition was truncated
			 * and needs to be erased */
			ESP_ERROR_CHECK(nvs_flash_erase());

			/* Retry nvs_flash_init */
			ESP_ERROR_CHECK(nvs_flash_init());
		}
	}

    ESP_ERROR_CHECK(esp_netif_init());
    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

//    wifi_prov_mgr_config_t wifi_prov_mgr_config = {
//        .scheme = wifi_prov_scheme_ble,
//        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
//    };
//    ESP_ERROR_CHECK(wifi_prov_mgr_init(wifi_prov_mgr_config));
//
//
//    bool provisioned = false;
//    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
//
//    if(!provisioned) {
//        uint8_t custom_service_uuid[] = {
//            /* LSB <---------------------------------------
//             * ---------------------------------------> MSB */
//            0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
//            0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
//        };
//
//        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
////        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_0, NULL, "SAM-MAQLAVAR", NULL));
////		wifi_prov_mgr_wait();
////		wifi_prov_mgr_deinit();
//    }

	vTaskDelay(portMAX_DELAY);
}

void vApplicationIdleHook() {

}
