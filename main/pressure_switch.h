/*
 * pressure_switch.h
 *
 *  Created on: May 15, 2024
 *      Author: samli
 */

#ifndef MAIN_PRESSURE_SWITCH_H_
#define MAIN_PRESSURE_SWITCH_H_

#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(PRESSURE_SWITCH_EVENT);

typedef enum {
	PRESSURE_SWITCH_EVENT_LV1_OPENED,
	PRESSURE_SWITCH_EVENT_LV1_CLOSED,
	PRESSURE_SWITCH_EVENT_LV2_OPENED,
	PRESSURE_SWITCH_EVENT_LV2_CLOSED,
	PRESSURE_SWITCH_EVENT_OVERFLOW_OPENED,
	PRESSURE_SWITCH_EVENT_OVERFLOW_CLOSED,
	PRESSURE_SWITCH_EVENT_LID_OPENED,
	PRESSURE_SWITCH_EVENT_LID_CLOSED,
} pressure_switch_event_t;


typedef struct {
	bool level_1 	: 1;
	bool level_2 	: 1;
	bool overflow 	: 1;
	bool lid		: 1;
} pressure_switch_state_t;

extern pressure_switch_state_t pressure_switch_state;

void __attribute__((noreturn)) pressure_switch_task_entry(void* params);

#endif /* MAIN_PRESSURE_SWITCH_H_ */
