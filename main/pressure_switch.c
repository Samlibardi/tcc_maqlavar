/*
 * pressure_switch.c
 *
 *  Created on: May 15, 2024
 *      Author: samli
 */

#include "pressure_switch.h"
#include "main.h"
#include "pca9554.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

ESP_EVENT_DEFINE_BASE(PRESSURE_SWITCH_EVENT);

pressure_switch_state_t pressure_switch_state;

static void IRAM_ATTR press_switch_gpio_isr_handler(void *arg) {
	TaskHandle_t task = (TaskHandle_t) arg;
	BaseType_t hptw;
	vTaskNotifyGiveFromISR(task, &hptw);
	portYIELD_FROM_ISR(hptw);
}

static void pressure_switch_update_state(uint8_t port_val) {
	pressure_switch_state.level_1 = !!(port_val & PCA_PIN_PRESS_SWITCH_LV_1);
	pressure_switch_state.level_2 = !!(port_val & PCA_PIN_PRESS_SWITCH_LV_1);
	pressure_switch_state.overflow = !!(port_val & PCA_PIN_OVERFLOW);
	pressure_switch_state.lid = !!(port_val & PCA_PIN_LID);
}

void __attribute__((noreturn)) pressure_switch_task_entry(void* params) {
	uint8_t port_val;
	pca9554_read_input(PCA_PRES_SWITCH, &port_val, pdMS_TO_TICKS(100));
	pressure_switch_update_state(port_val);

	gpio_set_direction(GPIO_NUM_18, GPIO_MODE_INPUT);
	gpio_isr_handler_add(GPIO_NUM_18, press_switch_gpio_isr_handler, (void*)xTaskGetCurrentTaskHandle());
	gpio_set_intr_type(GPIO_NUM_18, GPIO_INTR_NEGEDGE);
	gpio_intr_enable(GPIO_NUM_18);

	for(;;) {
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		uint8_t new_port_val;
		if(pca9554_read_input(PCA_PRES_SWITCH, &new_port_val, pdMS_TO_TICKS(100)) == ESP_OK) {
			uint8_t port_diff = new_port_val ^ port_val;
			port_val = new_port_val;
			pressure_switch_update_state(port_val);

			if(port_diff & PCA_PIN_PRESS_SWITCH_LV_1)
				esp_event_post(PRESSURE_SWITCH_EVENT, (port_val & PCA_PIN_PRESS_SWITCH_LV_1) ? PRESSURE_SWITCH_EVENT_LV1_CLOSED : PRESSURE_SWITCH_EVENT_LV1_OPENED, NULL, 0, 0);
			if(port_diff & PCA_PIN_PRESS_SWITCH_LV_2)
				esp_event_post(PRESSURE_SWITCH_EVENT, (port_val & PCA_PIN_PRESS_SWITCH_LV_2) ? PRESSURE_SWITCH_EVENT_LV2_CLOSED : PRESSURE_SWITCH_EVENT_LV2_OPENED, NULL, 0, 0);
			if(port_diff & PCA_PIN_OVERFLOW)
				esp_event_post(PRESSURE_SWITCH_EVENT, (port_val & PCA_PIN_OVERFLOW) ? PRESSURE_SWITCH_EVENT_OVERFLOW_CLOSED : PRESSURE_SWITCH_EVENT_OVERFLOW_OPENED, NULL, 0, 0);
			if(port_diff & PCA_PIN_LID)
				esp_event_post(PRESSURE_SWITCH_EVENT, (port_val & PCA_PIN_LID) ? PRESSURE_SWITCH_EVENT_LID_CLOSED : PRESSURE_SWITCH_EVENT_LID_OPENED, NULL, 0, 0);
		}
	}
}
