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
#include "pressure_switch.h"

typedef enum {
	VALVE_SOAP,
	VALVE_BLEACH,
	VALVE_SOFTENER,
	NUM_VALVES
} valve_enum_t;

#define SHAKING_PULSE_LENGTH_CW  250
#define SHAKING_PULSE_LENGTH_CCW 350
#define DUMP_TIME_SECONDS (6 * 60)

volatile wash_cycle_step_t wash_cycle_current_step;

#define SWITCH_EVENT_PRESSURE_LEVEL_1_CLOSED   (1 << 0)
#define SWITCH_EVENT_PRESSURE_LEVEL_1_OPENED   (1 << 1)
#define SWITCH_EVENT_PRESSURE_LEVEL_2_CLOSED   (1 << 2)
#define SWITCH_EVENT_PRESSURE_LEVEL_2_OPENED   (1 << 3)
#define SWITCH_EVENT_PRERSSURE_OVERFLOW_CLOSED  (1 << 4)
#define SWITCH_EVENT_PRESSURE_OVERFLOW_OPENED  (1 << 5)
#define SWITCH_EVENT_LID_CLOSED  (1 << 4)
#define SWITCH_EVENT_LID_OPENED  (1 << 5)

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

static TimerHandle_t wash_cycle_timer;
static TickType_t wash_cycle_wait_time_on_resume;

enum {
	WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV1_Pos,
	WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV2_Pos,
	WASH_CYCLE_NOTIFICATION_TIMER_Pos,
};

#define WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV1 (1 << WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV1_Pos)
#define WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV2 (1 << WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV2_Pos)
#define WASH_CYCLE_NOTIFICATION_TIMER (1 << WASH_CYCLE_NOTIFICATION_TIMER_Pos)
#define WASH_CYCLE_NOTIFICATIONS_ALL (WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV1 | WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV2 | WASH_CYCLE_NOTIFICATION_TIMER)


static void wash_cycle_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {

	uint32_t notification = 0;
	if(event_base == PRESSURE_SWITCH_EVENT) {
		switch(event_id) {
		case PRESSURE_SWITCH_EVENT_LV1_CLOSED:
			notification = WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV1;
			break;
		case PRESSURE_SWITCH_EVENT_LV2_CLOSED:
			notification = WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV2;
			break;
		default:
			break;
		}
	}

	if(notification)
		xTaskNotify(wash_cycle_task, notification, eSetBits);
}

static void wash_cycle_wait_fill(wash_cycle_water_level_t water_level) {
	switch(water_level) {
	case WASH_CYCLE_WATER_LEVEL_LOW:
		if(!pressure_switch_state.level_1) {
			uint32_t notifications;
			do {
				xTaskNotifyWait(0, WASH_CYCLE_NOTIFICATIONS_ALL, &notifications, portMAX_DELAY);
			} while(!(notifications & WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV1));
		}
		break;
	case WASH_CYCLE_WATER_LEVEL_MID:
		if(!pressure_switch_state.level_2) {
			uint32_t notifications;
			do {
				xTaskNotifyWait(0, WASH_CYCLE_NOTIFICATIONS_ALL, &notifications, portMAX_DELAY);
			} while(!(notifications & WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV2));
		}
		break;
	case WASH_CYCLE_WATER_LEVEL_HIGH:
		{
			if(!pressure_switch_state.level_1) {
				uint32_t notifications;
				do {
					xTaskNotifyWait(0, WASH_CYCLE_NOTIFICATIONS_ALL, &notifications, portMAX_DELAY);
				} while(!(notifications & WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV1));
			}

			TickType_t tL1 = xTaskGetTickCount();

			if(!pressure_switch_state.level_2) {
				uint32_t notifications;
				do {
					xTaskNotifyWait(0, WASH_CYCLE_NOTIFICATIONS_ALL, &notifications, portMAX_DELAY);
				} while(!(notifications & WASH_CYCLE_NOTIFICATION_PRESS_SWITCH_LV2));
			}

			TickType_t tL2 = xTaskGetTickCount();


			vTaskDelay((tL2 - tL1)/2);
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
	pca9554_output_clear_bits(PCA_VALVES, mask, portMAX_DELAY);
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
	pca9554_output_set_bits(PCA_VALVES, mask, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void wash_cycle_wait_seconds(uint32_t time_seconds) {
	xTimerChangePeriod(wash_cycle_timer, pdMS_TO_TICKS(1000 * time_seconds), portMAX_DELAY);
	uint32_t notifications = 0;
	do {
		xTaskNotifyWait(WASH_CYCLE_NOTIFICATION_TIMER, WASH_CYCLE_NOTIFICATION_TIMER, &notifications, portMAX_DELAY);
	} while (!(notifications & WASH_CYCLE_NOTIFICATION_TIMER) && xTimerIsTimerActive(wash_cycle_timer));
}

static void close_all_valves() {
	xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
	for(size_t i = 0; i < NUM_VALVES; i++)
		wash_cycle_state.valve[i] = false;
	pca9554_output_set_bits(PCA_VALVES, PCA_PIN_VALVE_1 | PCA_PIN_VALVE_2 | PCA_PIN_VALVE_3, portMAX_DELAY);
	xSemaphoreGive(wash_cycle_state_mutex);
}

static void wash_cycle_shake(uint16_t time_seconds, uint16_t period_ms) {

	TimeOut_t timeout;
	vTaskSetTimeOutState(&timeout);

	TickType_t loop_ticks = xTaskGetTickCount();
	bool i = 0;
	xTimerChangePeriod(wash_cycle_timer, pdMS_TO_TICKS(1000 * time_seconds), portMAX_DELAY);

	while(xTimerIsTimerActive(wash_cycle_timer)) {
		if(i) start_motor_cw(); else start_motor_ccw();
		vTaskDelay(i ? pdMS_TO_TICKS(SHAKING_PULSE_LENGTH_CW) : pdMS_TO_TICKS(SHAKING_PULSE_LENGTH_CCW));
		i = !i;
		stop_motor();

		vTaskDelayUntil(&loop_ticks, pdMS_TO_TICKS(period_ms/2));
	}
}

static void wash_cycle_dump() {
	start_pump();
	wash_cycle_wait_seconds(DUMP_TIME_SECONDS);
	stop_pump();
}

static void wash_cycle_centrifuge(uint16_t time_seconds) {
	start_pump();
	vTaskDelay(pdMS_TO_TICKS(1000));
	start_motor_cw();
	wash_cycle_wait_seconds(time_seconds);
	stop_motor();
	stop_pump();
}


static void wash_cycle_centrifuge_spray(uint16_t time_seconds) {
	start_pump();
	vTaskDelay(pdMS_TO_TICKS(1000));
	start_motor_cw();

	TimeOut_t timeout;
	vTaskSetTimeOutState(&timeout);

	TickType_t spray_tick = xTaskGetTickCount();
	xTimerChangePeriod(wash_cycle_timer, pdMS_TO_TICKS(1000 * time_seconds), portMAX_DELAY);

	while(xTimerIsTimerActive(wash_cycle_timer)) {
		vTaskDelayUntil(&spray_tick, pdMS_TO_TICKS(1000 * 15));
		open_valve(VALVE_BLEACH);
		vTaskDelay(pdMS_TO_TICKS(1000 * 3));
		close_valve(VALVE_BLEACH);
	}

	stop_motor();
	stop_pump();
}

static void wash_cycle_timer_cb (TimerHandle_t timer) {
	xTaskNotify(wash_cycle_task, WASH_CYCLE_NOTIFICATION_TIMER, eSetBits);
}

static void __attribute__((noreturn)) wash_cycle_task_entry(void* params) {
	if(wash_cycle_timer == NULL)
		wash_cycle_timer = xTimerCreate("wash_cycle_timer", 0, 0, NULL, wash_cycle_timer_cb);

	wash_cycle_params_t cycle_params = *(wash_cycle_params_t*)params;

	wash_cycle_current_step = WASH_CYCLE_STEP_PREWASH;

	if(cycle_params.prewash_time) {
		open_valve(VALVE_BLEACH);
		wash_cycle_wait_fill(cycle_params.water_level);
		close_valve(VALVE_BLEACH);

		wash_cycle_shake(cycle_params.prewash_time, cycle_params.prewash_shaking_period);

		if(cycle_params.prewash_break_time) {
			wash_cycle_wait_seconds(cycle_params.prewash_break_time);
		}

		wash_cycle_dump();
	}

	wash_cycle_current_step = WASH_CYCLE_STEP_BREAK;
	esp_event_post(WASH_CYCLE_EVENT, WASH_CYCLE_EVENT_STEP, NULL, 0, 0);

	open_valve(VALVE_SOAP);
	wash_cycle_wait_fill(cycle_params.water_level);
	close_valve(VALVE_SOAP);

	if(cycle_params.break_time) {
		wash_cycle_wait_seconds(cycle_params.break_time);
	}

	wash_cycle_current_step = WASH_CYCLE_STEP_WASH;
	esp_event_post(WASH_CYCLE_EVENT, WASH_CYCLE_EVENT_STEP, NULL, 0, 0);

	wash_cycle_shake(cycle_params.wash_time, cycle_params.wash_shaking_period);

	wash_cycle_dump();

	wash_cycle_current_step = WASH_CYCLE_STEP_RINSE;
	esp_event_post(WASH_CYCLE_EVENT, WASH_CYCLE_EVENT_STEP, NULL, 0, 0);

	for(typeof(cycle_params.rinse_count) i = 0; i < cycle_params.rinse_count; i++) {
		valve_enum_t valve = (i < cycle_params.rinse_count - 1) ? VALVE_SOAP : VALVE_SOFTENER;

		open_valve(valve);
		wash_cycle_wait_fill(cycle_params.water_level);
		close_valve(valve);

		wash_cycle_dump();
		wash_cycle_centrifuge_spray(60 * 3 / 2);
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

	if(wash_cycle_timer) {
		xTimerStop(wash_cycle_timer, portMAX_DELAY);
		xTimerDelete(wash_cycle_timer, portMAX_DELAY);
		wash_cycle_timer = NULL;
	}

	wash_cycle_task = NULL;
	vTaskDelete(NULL);
	__builtin_unreachable();
}

bool wash_cycle_start(wash_cycle_params_t* params) {
	if(wash_cycle_state_mutex == NULL)
		wash_cycle_state_mutex = xSemaphoreCreateMutex();


	if(wash_cycle_task == NULL) {
		esp_event_handler_register(PRESSURE_SWITCH_EVENT, ESP_EVENT_ANY_ID, wash_cycle_event_handler, NULL);
		xTaskCreate(wash_cycle_task_entry, "Wash Cycle", 1536, (void*)params, WASH_CYCLE_TASK_PRIORITY, &wash_cycle_task);
		return true;
	}
	else return false;
}

void wash_cycle_pause() {
	if(wash_cycle_task != NULL) {
		xSemaphoreTake(wash_cycle_state_mutex, portMAX_DELAY);
		vTaskSuspend(wash_cycle_task);
		if(wash_cycle_timer != NULL && xTimerIsTimerActive(wash_cycle_timer)) {
			wash_cycle_wait_time_on_resume = xTimerGetExpiryTime(wash_cycle_timer) - xTaskGetTickCount();
			xTimerStop(wash_cycle_timer, portMAX_DELAY);
		} else
			wash_cycle_wait_time_on_resume = 0;
		xSemaphoreGive(wash_cycle_state_mutex);
		wash_cycle_paused_state = wash_cycle_state;
		stop_motor_pump();
		close_all_valves();
	}
}

void wash_cycle_resume() {
	if(wash_cycle_task != NULL) {
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

		if(wash_cycle_timer != NULL && wash_cycle_wait_time_on_resume) {
			xTimerChangePeriod(wash_cycle_timer, wash_cycle_wait_time_on_resume, portMAX_DELAY);
		}

		vTaskResume(wash_cycle_task);
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
		esp_event_handler_unregister(PRESSURE_SWITCH_EVENT, ESP_EVENT_ANY_ID, wash_cycle_event_handler);
	}
}
