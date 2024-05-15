/*
 * iopanel.c
 *
 *  Created on: May 3, 2024
 *      Author: samli
 */
#include "iopanel.h"
#include <stdbool.h>
#include "main.h"
#include "driver/gpio.h"
#include "pca9554.h"
#include "freertos/FreeRTOS.h"

ESP_EVENT_DEFINE_BASE(IOPANEL_EVENT);

uint8_t iopanel_current_row = 0;
volatile uint32_t iopanel_led_cols = 0;
uint8_t iopanel_btn_cols = 0;

#define IOPANEL_PCA_TIMEOUT 40


void __attribute__((noreturn)) iopanel_task_entry(void* params) {

	pca9554_write_config(PCA_IOPANEL, 0, IOPANEL_PCA_TIMEOUT); // all outputs

	gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT_OD);
	gpio_set_direction(GPIO_NUM_9, GPIO_MODE_OUTPUT_OD);

	gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
	gpio_set_direction(GPIO_NUM_1, GPIO_MODE_INPUT);

	TickType_t t0 = xTaskGetTickCount();

	for(;;) {
		pca9554_write_output(PCA_IOPANEL, 0, pdMS_TO_TICKS(5));

		gpio_set_level(GPIO_NUM_8, (iopanel_current_row & 1) ? 1 : 0);
		gpio_set_level(GPIO_NUM_9, (iopanel_current_row & 2) ? 1 : 0);

		pca9554_write_output(PCA_IOPANEL, (uint8_t)(iopanel_led_cols >> (8 * iopanel_current_row)), pdMS_TO_TICKS(5));

		// check if IO0 col is pressed
		typeof(iopanel_btn_cols) btn_mask = (1 << (2 * iopanel_current_row));
		if(gpio_get_level(GPIO_NUM_0)) {
			if(!(iopanel_btn_cols & btn_mask)) { // button just pressed
				switch(iopanel_current_row & 3) {
				case 0:
					esp_event_post(IOPANEL_EVENT, IOPANEL_EVENT_BUTTON_BREAK_DURATION, NULL, 0, 0);
					break;
				case 1:
					esp_event_post(IOPANEL_EVENT, IOPANEL_EVENT_BUTTON_CLOTHING_TYPE, NULL, 0, 0);
					break;
				case 2:
					esp_event_post(IOPANEL_EVENT, IOPANEL_EVENT_BUTTON_PROGRAM, NULL, 0, 0);
					break;
				case 3:
					esp_event_post(IOPANEL_EVENT, IOPANEL_EVENT_BUTTON_ONOFF, NULL, 0, 0);
					break;
				}
			}
			iopanel_btn_cols |= btn_mask;
		}
		else
			iopanel_btn_cols &= ~btn_mask;

		// check if IO1 col is pressed
		btn_mask = (1 << (2 * iopanel_current_row + 1));
		if(gpio_get_level(GPIO_NUM_1)) {
			if(!(iopanel_btn_cols & btn_mask)) { // button just pressed
				switch(iopanel_current_row & 3) {
				case 0:
					break;
				case 1:
					esp_event_post(IOPANEL_EVENT, IOPANEL_EVENT_BUTTON_START_PAUSE, NULL, 0, 0);
					break;
				case 2:
					esp_event_post(IOPANEL_EVENT, IOPANEL_EVENT_BUTTON_RINSE_COUNT, NULL, 0, 0);
					break;
				case 3:
					esp_event_post(IOPANEL_EVENT, IOPANEL_EVENT_BUTTON_WATER_LEVEL, NULL, 0, 0);
					break;
				}
			}
			iopanel_btn_cols |= btn_mask;
		}
		else
			iopanel_btn_cols &= ~btn_mask;


		vTaskDelayUntil(&t0, pdMS_TO_TICKS(5));

		iopanel_current_row = (iopanel_current_row + 1) & 3;
	}
}
