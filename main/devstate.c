/*
 * devstate.c
 *
 *  Created on: May 7, 2024
 *      Author: samli
 */

#include "devstate.h"
#include "iopanel.h"
#include "wash_cycle.h"
#include "pressure_switch.h"
#include "main.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>

typedef enum {
	DEVICE_STATE_STANDBY,
	DEVICE_STATE_WAITING,
	DEVICE_STATE_WASH_CYCLE_RUNNING,
	DEVICE_STATE_WASH_CYCLE_PAUSED,
	DEVICE_STATE_ERROR,
} device_state_t;

device_state_t device_state = DEVICE_STATE_STANDBY;

enum {
	DEVSTATE_NOTIFICATION_BUTTON_ONOFF_Pos,
	DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE_Pos,
	DEVSTATE_NOTIFICATION_BUTTON_WATER_LEVEL_Pos,
	DEVSTATE_NOTIFICATION_BUTTON_CLOTHING_TYPE_Pos,
	DEVSTATE_NOTIFICATION_BUTTON_PROGRAM_Pos,
	DEVSTATE_NOTIFICATION_BUTTON_RINSE_COUNT_Pos,
	DEVSTATE_NOTIFICATION_BUTTON_BREAK_TIME_Pos,
	DEVSTATE_NOTIFICATION_WASH_CYCLE_STEP_Pos,
	DEVSTATE_NOTIFICATION_WASH_CYCLE_FINISHED_Pos,
	DEVSTATE_NOTIFICATION_SWITCH_LID_OPENED_Pos,
	DEVSTATE_NOTIFICATION_SWITCH_LID_CLOSED_Pos,
};

#define DEVSTATE_NOTIFICATION_BUTTON_ONOFF (1 << DEVSTATE_NOTIFICATION_BUTTON_ONOFF_Pos)
#define DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE (1 << DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE_Pos)
#define DEVSTATE_NOTIFICATION_BUTTON_WATER_LEVEL (1 << DEVSTATE_NOTIFICATION_BUTTON_WATER_LEVEL_Pos)
#define DEVSTATE_NOTIFICATION_BUTTON_CLOTHING_TYPE (1 << DEVSTATE_NOTIFICATION_BUTTON_CLOTHING_TYPE_Pos)
#define DEVSTATE_NOTIFICATION_BUTTON_PROGRAM (1 << DEVSTATE_NOTIFICATION_BUTTON_PROGRAM_Pos)
#define DEVSTATE_NOTIFICATION_BUTTON_RINSE_COUNT (1 << DEVSTATE_NOTIFICATION_BUTTON_RINSE_COUNT_Pos)
#define DEVSTATE_NOTIFICATION_BUTTON_BREAK_DURATION (1 << DEVSTATE_NOTIFICATION_BUTTON_BREAK_TIME_Pos)
#define DEVSTATE_NOTIFICATION_WASH_CYCLE_STEP (1 << DEVSTATE_NOTIFICATION_WASH_CYCLE_STEP_Pos)
#define DEVSTATE_NOTIFICATION_WASH_CYCLE_FINISHED (1 << DEVSTATE_NOTIFICATION_WASH_CYCLE_FINISHED_Pos)
#define DEVSTATE_NOTIFICATION_SWITCH_LID_OPENED (1 << DEVSTATE_NOTIFICATION_SWITCH_LID_OPENED_Pos)
#define DEVSTATE_NOTIFICATION_SWITCH_LID_CLOSED (1 << DEVSTATE_NOTIFICATION_SWITCH_LID_CLOSED_Pos)

#define BLINK_TIME_MS 700

static wash_cycle_water_level_t selected_water_level = WASH_CYCLE_WATER_LEVEL_LOW;

typedef enum {
	SEL_CLOTHING_TYPE_COLOR,
	SEL_CLOTHING_TYPE_WHITE,
	SEL_CLOTHING_TYPE_DELICATE,
	NUM_SEL_CLOTHING_TYPES,
} sel_clothing_type_t;

static sel_clothing_type_t selected_clothing_type = SEL_CLOTHING_TYPE_COLOR;

static wash_cycle_step_t selected_program = WASH_CYCLE_STEP_PREWASH;

#define MAX_RINSE_COUNT 3
static uint8_t selected_rinse_count = 1;


typedef enum {
	SEL_BREAK_DURATION_SHORT,
	SEL_BREAK_DURATION_LONG,
	NUM_SEL_BREAK_DURATIONS,
} sel_break_duration_t;

sel_break_duration_t selected_break_duration = SEL_BREAK_DURATION_SHORT;

wash_cycle_params_t panel_wash_cycle_params = {
	.water_level = WASH_CYCLE_WATER_LEVEL_LOW,
	.prewash_break_time = 60 * 15,
	.prewash_time = 60 * 4,
	.prewash_shaking_period = 882,
	.break_time = 60 * 4,
	.wash_time = 60 * 5,
	.wash_shaking_period = 882,
	.rinse_count = 3,
	.centrifuge_time = 60 * 4,
};

void devstate_update_panel() {

	{
		iopanel_led_t water_level_led = IOPANEL_LED_NONE;
		switch(selected_water_level) {
		case WASH_CYCLE_WATER_LEVEL_LOW:
			water_level_led = IOPANEL_LED_WATER_LEVEL_LOW;
			break;
		case WASH_CYCLE_WATER_LEVEL_MID:
			water_level_led = IOPANEL_LED_WATER_LEVEL_MID;
			break;
		case WASH_CYCLE_WATER_LEVEL_HIGH:
			water_level_led = IOPANEL_LED_WATER_LEVEL_HIGH;
			break;
		case NUM_SEL_CLOTHING_TYPES:
			__builtin_unreachable();
			break;
		}
		iopanel_set_clear_leds(water_level_led, IOPANEL_LEDS_WATER_LEVEL_ALL);
	}

	{
		iopanel_led_t clothing_type_led = IOPANEL_LED_NONE;
		switch(selected_clothing_type) {
		case SEL_CLOTHING_TYPE_COLOR:
			clothing_type_led = IOPANEL_LED_CLOTHING_TYPE_COLOR;
			break;
		case SEL_CLOTHING_TYPE_WHITE:
			clothing_type_led = IOPANEL_LED_CLOTHING_TYPE_WHITE;
			break;
		case SEL_CLOTHING_TYPE_DELICATE:
			clothing_type_led = IOPANEL_LED_CLOTHING_TYPE_DELICATE;
			break;
		case NUM_SEL_CLOTHING_TYPES:
			__builtin_unreachable();
			break;
		}
		iopanel_set_clear_leds(clothing_type_led, IOPANEL_LEDS_CLOTHING_TYPE_ALL);
	}

	switch(device_state) {
	case DEVICE_STATE_WAITING:
	case DEVICE_STATE_WASH_CYCLE_PAUSED:
	case DEVICE_STATE_WASH_CYCLE_RUNNING: {
		uint32_t program_leds = 0;

		wash_cycle_step_t tmp_step = (device_state == DEVICE_STATE_WAITING) ? selected_program : wash_cycle_current_step;
		if(tmp_step <= WASH_CYCLE_STEP_PREWASH)
			program_leds |= IOPANEL_LED_PROGRAM_PREWASH;
		if(tmp_step <= WASH_CYCLE_STEP_BREAK)
			program_leds |= IOPANEL_LED_PROGRAM_BREAK;
		if(tmp_step <= WASH_CYCLE_STEP_WASH)
			program_leds |= IOPANEL_LED_PROGRAM_WASH;
		if(tmp_step <= WASH_CYCLE_STEP_RINSE)
			program_leds |= IOPANEL_LED_PROGRAM_RINSE;
		if(tmp_step <= WASH_CYCLE_STEP_CENTRIFUGE)
			program_leds |= IOPANEL_LED_PROGRAM_CENTRIFUGE;

		iopanel_set_clear_leds(program_leds, IOPANEL_LEDS_PROGRAM_ALL);
		break;
	}
	default:
		iopanel_clear_leds(IOPANEL_LEDS_PROGRAM_ALL);
	}

	{
		iopanel_led_t rinse_count_led = IOPANEL_LED_NONE;
		switch(selected_rinse_count) {
		case 1:
			rinse_count_led = IOPANEL_LED_RINSE_COUNT_1;
			break;
		case 2:
			rinse_count_led = IOPANEL_LED_RINSE_COUNT_2;
			break;
		case 3:
			rinse_count_led = IOPANEL_LED_RINSE_COUNT_3;
			break;
		}
		iopanel_set_clear_leds(rinse_count_led, IOPANEL_LEDS_RINSE_COUNT_ALL);
	}

	{
		iopanel_led_t break_duration_led = IOPANEL_LED_NONE;
		switch(selected_break_duration) {
		case SEL_BREAK_DURATION_LONG:
			break_duration_led = IOPANEL_LED_BREAK_DURATION_LONG;
			break;
		case SEL_BREAK_DURATION_SHORT:
			break_duration_led = IOPANEL_LED_BREAK_DURATION_SHORT;
			break;
		case NUM_SEL_BREAK_DURATIONS:
			__builtin_unreachable();
			break;
		}
		iopanel_set_clear_leds(break_duration_led, IOPANEL_LEDS_BREAK_DURATION_ALL);
	}
}

static void devstate_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	TaskHandle_t devstate_task = (TaskHandle_t)event_handler_arg;
	uint32_t notification = 0;
	if(event_base == IOPANEL_EVENT) {
		switch(event_id) {
		case IOPANEL_EVENT_BUTTON_ONOFF:
			notification = DEVSTATE_NOTIFICATION_BUTTON_ONOFF;
			break;
		case IOPANEL_EVENT_BUTTON_START_PAUSE:
			notification = DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE;
			break;
		case IOPANEL_EVENT_BUTTON_WATER_LEVEL:
			notification = DEVSTATE_NOTIFICATION_BUTTON_WATER_LEVEL;
			break;
		case IOPANEL_EVENT_BUTTON_CLOTHING_TYPE:
			notification = DEVSTATE_NOTIFICATION_BUTTON_CLOTHING_TYPE;
			break;
		case IOPANEL_EVENT_BUTTON_PROGRAM:
			notification = DEVSTATE_NOTIFICATION_BUTTON_PROGRAM;
			break;
		case IOPANEL_EVENT_BUTTON_RINSE_COUNT:
			notification = DEVSTATE_NOTIFICATION_BUTTON_RINSE_COUNT;
			break;
		case IOPANEL_EVENT_BUTTON_BREAK_DURATION:
			notification = DEVSTATE_NOTIFICATION_BUTTON_BREAK_DURATION;
			break;
		default:
			break;
		}
	}
	else if(event_base == WASH_CYCLE_EVENT) {
		switch(event_id) {
		case WASH_CYCLE_EVENT_STEP:
			notification = DEVSTATE_NOTIFICATION_WASH_CYCLE_STEP;
			break;
		case WASH_CYCLE_EVENT_FINISHED:
			notification = DEVSTATE_NOTIFICATION_WASH_CYCLE_FINISHED;
			break;
		default:
			break;
		}
	}
	else if(event_base == PRESSURE_SWITCH_EVENT) {
		switch(event_id) {
		case PRESSURE_SWITCH_EVENT_LID_CLOSED:
			notification = DEVSTATE_NOTIFICATION_SWITCH_LID_CLOSED;
			break;
		case PRESSURE_SWITCH_EVENT_LID_OPENED:
			notification = DEVSTATE_NOTIFICATION_SWITCH_LID_OPENED;
			break;
		default:
			break;
		}
	}

	if(notification)
		xTaskNotify(devstate_task, notification, eSetBits);
}

void __attribute__((noreturn)) devstate_task_entry (void* params){
	esp_event_handler_register(IOPANEL_EVENT, ESP_EVENT_ANY_ID, devstate_event_handler, (void*)xTaskGetCurrentTaskHandle());
	esp_event_handler_register(WASH_CYCLE_EVENT, ESP_EVENT_ANY_ID, devstate_event_handler, (void*)xTaskGetCurrentTaskHandle());
	esp_event_handler_register(PRESSURE_SWITCH_EVENT, ESP_EVENT_ANY_ID, devstate_event_handler, (void*)xTaskGetCurrentTaskHandle());


	TickType_t ticksBlink = pdMS_TO_TICKS(BLINK_TIME_MS);
	TimeOut_t timeoutBlink;

	for(;;) {
		device_state_t next_device_state = device_state;
		switch(device_state) {
		case DEVICE_STATE_STANDBY: {
			uint32_t notifications;
			xTaskNotifyWait(0, DEVSTATE_NOTIFICATION_BUTTON_ONOFF, &notifications, portMAX_DELAY);

			iopanel_set_leds(IOPANEL_LEDS_ALL);
			vTaskDelay(pdMS_TO_TICKS(1000));
			next_device_state = DEVICE_STATE_WAITING;
			vTaskSetTimeOutState(&timeoutBlink);
			ticksBlink = pdMS_TO_TICKS(BLINK_TIME_MS);
			break;
		}
		case DEVICE_STATE_WAITING: {
			devstate_update_panel();
			if(xTaskCheckForTimeOut(&timeoutBlink, &ticksBlink)) {
				iopanel_toggle_leds(IOPANEL_LED_ONOFF);
				ticksBlink = pdMS_TO_TICKS(BLINK_TIME_MS);
				vTaskSetTimeOutState(&timeoutBlink);
			}

			uint32_t notifications;
			if(xTaskNotifyWait(0,
					DEVSTATE_NOTIFICATION_BUTTON_ONOFF
					| DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE
					| DEVSTATE_NOTIFICATION_BUTTON_WATER_LEVEL
					| DEVSTATE_NOTIFICATION_BUTTON_CLOTHING_TYPE
					| DEVSTATE_NOTIFICATION_BUTTON_PROGRAM
					| DEVSTATE_NOTIFICATION_BUTTON_RINSE_COUNT
					| DEVSTATE_NOTIFICATION_BUTTON_BREAK_DURATION
					, &notifications, ticksBlink) == pdPASS) {
				if(notifications & DEVSTATE_NOTIFICATION_BUTTON_ONOFF) {
					iopanel_clear_leds(IOPANEL_LEDS_ALL);
					next_device_state = DEVICE_STATE_STANDBY;
				}
				else if(notifications & DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE) {
					panel_wash_cycle_params.water_level = selected_water_level;
					//TODO config wash cycle params
					switch(selected_clothing_type) {
					case SEL_CLOTHING_TYPE_COLOR:
					case SEL_CLOTHING_TYPE_WHITE:
						panel_wash_cycle_params.prewash_shaking_period = panel_wash_cycle_params.wash_shaking_period = 882;
						break;
					case SEL_CLOTHING_TYPE_DELICATE:
						panel_wash_cycle_params.prewash_shaking_period = panel_wash_cycle_params.wash_shaking_period = 1200;
						break;
					case NUM_SEL_CLOTHING_TYPES:
						__builtin_unreachable();
					}

					if(wash_cycle_start(&panel_wash_cycle_params)) {
						iopanel_set_leds(IOPANEL_LED_ONOFF);
						next_device_state = DEVICE_STATE_WASH_CYCLE_RUNNING;
					}

				} else if(notifications & DEVSTATE_NOTIFICATION_BUTTON_WATER_LEVEL) {
					typeof(selected_water_level) tmp_water_level = selected_water_level + 1;
					if(tmp_water_level >= WASH_CYCLE_NUM_WATER_LEVELS)
						tmp_water_level = 0;
					selected_water_level = tmp_water_level;
				} else if(notifications & DEVSTATE_NOTIFICATION_BUTTON_CLOTHING_TYPE) {
					typeof(selected_clothing_type) tmp_clothing_type = selected_clothing_type + 1;
					if(tmp_clothing_type >= NUM_SEL_CLOTHING_TYPES)
						tmp_clothing_type = 0;
					selected_clothing_type = tmp_clothing_type;
				} else if(notifications & DEVSTATE_NOTIFICATION_BUTTON_PROGRAM) {
					typeof(selected_program) tmp_program = selected_program + 1;
					if(tmp_program >= WASH_CYCLE_NUM_STEPS)
						tmp_program = 0;
					selected_program = tmp_program;
				} else if(notifications & DEVSTATE_NOTIFICATION_BUTTON_RINSE_COUNT) {
					typeof(selected_rinse_count) tmp_rinse_count = selected_rinse_count + 1;
					if(tmp_rinse_count > MAX_RINSE_COUNT)
						tmp_rinse_count = 1;
					selected_rinse_count = tmp_rinse_count;
				} else if(notifications & DEVSTATE_NOTIFICATION_BUTTON_BREAK_DURATION) {
					typeof(selected_break_duration) tmp_break_duration = selected_break_duration + 1;
					if(tmp_break_duration >= NUM_SEL_BREAK_DURATIONS)
						tmp_break_duration = 0;
					selected_break_duration = tmp_break_duration;
				}
			}
			break;
		}
		case DEVICE_STATE_WASH_CYCLE_RUNNING: {
			uint32_t notifications;
			xTaskNotifyWait(0, (DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE | DEVSTATE_NOTIFICATION_WASH_CYCLE_STEP | DEVSTATE_NOTIFICATION_WASH_CYCLE_FINISHED), &notifications, portMAX_DELAY);

			if(notifications & DEVSTATE_NOTIFICATION_WASH_CYCLE_FINISHED) {
				iopanel_clear_leds(IOPANEL_LEDS_ALL);
				next_device_state = DEVICE_STATE_WAITING;
			} else if(notifications & DEVSTATE_NOTIFICATION_WASH_CYCLE_STEP) {
				devstate_update_panel();
			} else if(notifications & (DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE | DEVSTATE_NOTIFICATION_SWITCH_LID_OPENED)) {
				wash_cycle_pause();
				next_device_state = DEVICE_STATE_WASH_CYCLE_PAUSED;
			}
			break;
		}
		case DEVICE_STATE_WASH_CYCLE_PAUSED: {
			uint32_t notifications;
			if(xTaskNotifyWait(0, DEVSTATE_NOTIFICATION_BUTTON_ONOFF | DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE, &notifications, pdMS_TO_TICKS(BLINK_TIME_MS)) == pdPASS) {
				if(notifications & DEVSTATE_NOTIFICATION_BUTTON_ONOFF) {
					wash_cycle_abort();
					iopanel_clear_leds(IOPANEL_LEDS_ALL);
					next_device_state = DEVICE_STATE_STANDBY;
				}
				else if((notifications & (DEVSTATE_NOTIFICATION_BUTTON_START_PAUSE | DEVSTATE_NOTIFICATION_SWITCH_LID_CLOSED)) && pressure_switch_state.lid) {
					wash_cycle_resume();
					iopanel_set_leds(IOPANEL_LED_ONOFF);
					next_device_state = DEVICE_STATE_WASH_CYCLE_RUNNING;
				}
			}
			else {
				iopanel_toggle_leds(IOPANEL_LED_ONOFF);
			}
			break;
		}
		case DEVICE_STATE_ERROR:
			for(;;) {
				iopanel_set_leds(IOPANEL_LEDS_ALL);
				vTaskDelay(pdMS_TO_TICKS(500));
				iopanel_clear_leds(IOPANEL_LEDS_ALL);
				vTaskDelay(pdMS_TO_TICKS(500));
			}
			break;
		}

		device_state = next_device_state;

	}
}
