/*
 * wash_cycle.c
 *
 *  Created on: Apr 30, 2024
 *      Author: samli
 */

#include "wash_cycle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "main.h"
#include "pca9554.h"

typedef struct {
	uint8_t water_level;
	uint8_t prewash_shaking_period;
	uint8_t wash_shaking_period;
	uint8_t rinse_count;
	uint16_t prewash_time;
	uint16_t prewash_break_time;
	uint16_t break_time;
	uint16_t wash_time;
	uint16_t centrifuge_time;
} wash_cycle_params_t;

typedef enum {
	VALVE_SOAP,
	VALVE_BLEACH,
	VALVE_SOFTENER,
	NUM_VALVES
} valve_enum_t;

#define SHAKING_PULSE_LENGTH 50
#define DUMP_TIME_MS (6 * 60 * 1000)

volatile wash_cycle_step_t wash_cycle_current_step;

static EventGroupHandle_t switch_event_group = NULL;

#define SWITCH_EVENT_PRESSURE_LEVEL_1_CLOSED   (1 << 0)
#define SWITCH_EVENT_PRESSURE_LEVEL_1_OPENED   (1 << 1)
#define SWITCH_EVENT_PRESSURE_LEVEL_2_CLOSED   (1 << 2)
#define SWITCH_EVENT_PRESSURE_LEVEL_2_OPENED   (1 << 3)
#define SWITCH_EVENT_PRERSSURE_OVERFLOW_CLOSED  (1 << 4)
#define SWITCH_EVENT_PRESSURE_OVERFLOW_OPENED  (1 << 5)
#define SWITCH_EVENT_LID_CLOSED  (1 << 4)
#define SWITCH_EVENT_LID_OPENED  (1 << 5)

typedef struct switch_state {
	bool switch_pressure_level_1 	: 1;
	bool switch_pressure_level_2 	: 1;
	bool switch_pressure_overflow 	: 1;
	bool switch_lid					: 1;
} switch_state_t;

static switch_state_t switch_state;

ESP_EVENT_DEFINE_BASE(WASH_CYCLE_EVENT);

static TaskHandle_t wash_cycle_task = NULL;

typedef struct {
	bool motor_cw;
	bool motor_ccw;
	bool pump;
	bool valve[NUM_VALVES];
} wash_cycle_state_t;
static wash_cycle_state_t wash_cycle_state = {};
static wash_cycle_state_t wash_cycle_paused_state = {};

static SemaphoreHandle_t wash_cycle_state_mutex = NULL;

static void wash_cycle_wait_fill(wash_cycle_water_level_t water_level) {
	switch(water_level) {
	case WASH_CYCLE_WATER_LEVEL_LOW:
		while(!switch_state.switch_pressure_level_1) {
			xEventGroupWaitBits(switch_event_group, SWITCH_EVENT_PRESSURE_LEVEL_1_CLOSED, pdTRUE, pdFALSE, portMAX_DELAY);
		}
		break;
	case WASH_CYCLE_WATER_LEVEL_MID:
		while(!switch_state.switch_pressure_level_2) {
			xEventGroupWaitBits(switch_event_group, SWITCH_EVENT_PRESSURE_LEVEL_2_CLOSED, pdTRUE, pdFALSE, portMAX_DELAY);
		}
		break;
	case WASH_CYCLE_WATER_LEVEL_HIGH:
		{
			while(!switch_state.switch_pressure_level_1) {
				xEventGroupWaitBits(switch_event_group, SWITCH_EVENT_PRESSURE_LEVEL_1_CLOSED, pdTRUE, pdFALSE, portMAX_DELAY);
			}

			TickType_t tL1 = xTaskGetTickCount();

			while(!switch_state.switch_pressure_level_2) {
				xEventGroupWaitBits(switch_event_group, SWITCH_EVENT_PRESSURE_LEVEL_2_CLOSED, pdTRUE, pdFALSE, portMAX_DELAY);
			}

			TickType_t tL2 = xTaskGetTickCount();


			vTaskDelay(tL2 - tL1);

		}
		break;
	case WASH_CYCLE_NUM_WATER_LEVELS:
		__builtin_unreachable();
	}
}

static void start_motor_cw() {
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	wash_cycle_state.motor_cw = true;
	pca9554_output_set_clear_bits(PCA_MOTOR_PUMP, PCA_PIN_MOTOR_CCW, PCA_PIN_MOTOR_CW, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void start_motor_ccw() {
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	wash_cycle_state.motor_ccw = true;
	pca9554_output_set_clear_bits(PCA_MOTOR_PUMP, PCA_PIN_MOTOR_CW, PCA_PIN_MOTOR_CCW, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void stop_motor() {
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	wash_cycle_state.motor_cw = wash_cycle_state.motor_ccw = false;
	pca9554_output_set_bits(PCA_MOTOR_PUMP, PCA_PIN_MOTOR_CW | PCA_PIN_MOTOR_CCW, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void start_pump() {
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	wash_cycle_state.pump = true;
	pca9554_output_clear_bits(PCA_MOTOR_PUMP, PCA_PIN_PUMP, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void stop_pump() {
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	wash_cycle_state.pump = false;
	pca9554_output_set_bits(PCA_MOTOR_PUMP, PCA_PIN_PUMP, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void stop_motor_pump() {
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	wash_cycle_state.motor_cw = wash_cycle_state.motor_ccw = wash_cycle_state.pump = false;
	pca9554_output_set_bits(PCA_MOTOR_PUMP, PCA_PIN_PUMP | PCA_PIN_MOTOR_CW | PCA_PIN_MOTOR_CCW, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void open_valve(valve_enum_t v) {
	uint8_t mask;
	switch(v) {
	case VALVE_BLEACH:
		mask = PCA_PIN_VALVE_1;
		break;
	case VALVE_SOAP:
		mask = PCA_PIN_VALVE_2;
		break;
	case VALVE_SOFTENER:
		mask = PCA_PIN_VALVE_3;
		break;
	default:
		mask = 0;
		break;
	}
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	wash_cycle_state.valve[v] = true;
	pca9554_output_clear_bits(PCA_VALVES_PRES_SWITCH, mask, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void close_valve(valve_enum_t v) {
	uint8_t mask;
	switch(v) {
	case VALVE_BLEACH:
		mask = PCA_PIN_VALVE_1;
		break;
	case VALVE_SOAP:
		mask = PCA_PIN_VALVE_2;
		break;
	case VALVE_SOFTENER:
		mask = PCA_PIN_VALVE_3;
		break;
	default:
		mask = 0;
		break;
	}
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	wash_cycle_state.valve[v] = false;
	pca9554_output_set_bits(PCA_VALVES_PRES_SWITCH, mask, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void close_all_valves() {
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	for(size_t i = 0; i < NUM_VALVES; i++)
		wash_cycle_state.valve[i] = false;
	pca9554_output_set_bits(PCA_VALVES_PRES_SWITCH, PCA_PIN_VALVE_1 | PCA_PIN_VALVE_2 | PCA_PIN_VALVE_3, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void wash_cycle_shake(uint16_t time_seconds, uint16_t period_ms) {

	TimeOut_t timeout;
	vTaskSetTimeOutState(&timeout);

	TickType_t wait_ticks = pdMS_TO_TICKS(1000 * time_seconds);
	while(xTaskCheckForTimeOut(&timeout, &wait_ticks) == pdFALSE) {
		TickType_t t_start = xTaskGetTickCount();

		start_motor_cw();
		vTaskDelay(pdMS_TO_TICKS(SHAKING_PULSE_LENGTH));
		stop_motor();

		vTaskDelayUntil(&t_start, pdMS_TO_TICKS(period_ms/2));

		start_motor_ccw();
		vTaskDelay(pdMS_TO_TICKS(SHAKING_PULSE_LENGTH));
		stop_motor();

		vTaskDelayUntil(&t_start, pdMS_TO_TICKS(period_ms));
	}
}

static void wash_cycle_dump() {
	start_pump();
	vTaskDelay(pdMS_TO_TICKS(DUMP_TIME_MS));
	stop_pump();
}

static void wash_cycle_centrifuge(uint16_t time_seconds) {
	start_pump();
	vTaskDelay(pdMS_TO_TICKS(1000));
	start_motor_cw();
	vTaskDelay(pdMS_TO_TICKS(time_seconds * 1000));
	stop_motor();
}

static void __attribute__((noreturn)) wash_cycle_task_entry(void* params) {

	if(switch_event_group == NULL)
		switch_event_group = xEventGroupCreate();

	wash_cycle_params_t* cycle_params = 0;

	wash_cycle_current_step = WASH_CYCLE_STEP_PREWASH;

	if(cycle_params->prewash_time) {
		open_valve(VALVE_BLEACH);
		wash_cycle_wait_fill(cycle_params->water_level);
		close_valve(VALVE_BLEACH);

		wash_cycle_shake(cycle_params->prewash_time, cycle_params->prewash_shaking_period);

		if(cycle_params->prewash_break_time) {
			vTaskDelay(pdMS_TO_TICKS(cycle_params->prewash_break_time * 1000));
		}

		wash_cycle_dump();
	}

	wash_cycle_current_step = WASH_CYCLE_STEP_BREAK;
	esp_event_post(WASH_CYCLE_EVENT, WASH_CYCLE_EVENT_STEP, NULL, 0, 0);

	open_valve(VALVE_SOAP);
	wash_cycle_wait_fill(cycle_params->water_level);
	close_valve(VALVE_SOAP);

	if(cycle_params->break_time) {
		vTaskDelay(pdMS_TO_TICKS(cycle_params->break_time * 1000));
	}

	wash_cycle_current_step = WASH_CYCLE_STEP_WASH;
	esp_event_post(WASH_CYCLE_EVENT, WASH_CYCLE_EVENT_STEP, NULL, 0, 0);

	wash_cycle_shake(cycle_params->wash_time, cycle_params->wash_shaking_period);

	wash_cycle_dump();

	wash_cycle_current_step = WASH_CYCLE_STEP_RINSE;
	esp_event_post(WASH_CYCLE_EVENT, WASH_CYCLE_EVENT_STEP, NULL, 0, 0);

	for(typeof(cycle_params->rinse_count) i = 0; i < cycle_params->rinse_count; i++) {
		valve_enum_t valve = (i < cycle_params->rinse_count - 1) ? VALVE_SOAP : VALVE_SOFTENER;

		open_valve(valve);
		wash_cycle_wait_fill(cycle_params->water_level);
		close_valve(valve);

		wash_cycle_dump();
		wash_cycle_centrifuge(60 * 3 / 2);
	}

	wash_cycle_current_step = WASH_CYCLE_STEP_CENTRIFUGE;
	esp_event_post(WASH_CYCLE_EVENT, WASH_CYCLE_EVENT_STEP, NULL, 0, 0);

	wash_cycle_centrifuge(60 * 40);

	esp_event_post(WASH_CYCLE_EVENT, WASH_CYCLE_EVENT_FINISHED, NULL, 0, 0);

	wash_cycle_task = NULL;
	vTaskDelete(NULL);
	__builtin_unreachable();

}

static void __attribute__((noreturn)) wash_cycle_cleanup_task_entry(void* params) {
	close_all_valves();
	stop_motor();

	wash_cycle_dump();

	wash_cycle_task = NULL;
	vTaskDelete(NULL);
	__builtin_unreachable();
}

void wash_cycle_start() {
	if(wash_cycle_state_mutex == NULL)
		wash_cycle_state_mutex = xSemaphoreCreateMutex();

	if(wash_cycle_task == NULL)
		xTaskCreate(wash_cycle_task_entry, "Wash Cycle", 1024, NULL, WASH_CYCLE_TASK_PRIORITY, &wash_cycle_task);
	else {
		if(wash_cycle_paused_state.motor_ccw)
			start_motor_ccw();
		else if(wash_cycle_paused_state.motor_cw)
			start_motor_cw();

		if(wash_cycle_paused_state.pump)
			start_pump();

		for(size_t i = 0; i < NUM_VALVES; i++) {
			if(wash_cycle_paused_state.valve[i])
				open_valve(i);
		}

		vTaskResume(wash_cycle_task);
	}
}

void wash_cycle_pause() {
	if(wash_cycle_task != NULL) {
		xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
		vTaskSuspend(wash_cycle_task);
		xSemaphoreGive(wash_cycle_state_mutex);
		wash_cycle_paused_state = wash_cycle_state;
		stop_motor_pump();
		close_all_valves();
	}
}

void wash_cycle_skip_step() {

}

void wash_cycle_abort() {
	if(wash_cycle_task != NULL) {
		xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
		vTaskDelete(wash_cycle_task);
		xSemaphoreGive(wash_cycle_state_mutex);
		xTaskCreate(wash_cycle_cleanup_task_entry, "Wash Cycle Cleanup", 1024, NULL, WASH_CYCLE_TASK_PRIORITY, &wash_cycle_task);
	}
}
