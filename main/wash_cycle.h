/*
 * wash_cycle.h
 *
 *  Created on: May 6, 2024
 *      Author: samli
 */

#ifndef MAIN_WASH_CYCLE_H_
#define MAIN_WASH_CYCLE_H_

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(WASH_CYCLE_EVENT);

typedef enum {
	WASH_CYCLE_EVENT_STEP,
	WASH_CYCLE_EVENT_FINISHED
} wash_cycle_event_t;


typedef enum {
	WASH_CYCLE_WATER_LEVEL_LOW,
	WASH_CYCLE_WATER_LEVEL_MID,
	WASH_CYCLE_WATER_LEVEL_HIGH,
	WASH_CYCLE_NUM_WATER_LEVELS,
} wash_cycle_water_level_t;



typedef enum {
	WASH_CYCLE_STEP_PREWASH,
	WASH_CYCLE_STEP_BREAK,
	WASH_CYCLE_STEP_WASH,
	WASH_CYCLE_STEP_RINSE,
	WASH_CYCLE_STEP_CENTRIFUGE,
	WASH_CYCLE_NUM_STEPS,
} wash_cycle_step_t;

extern volatile wash_cycle_step_t wash_cycle_current_step;

typedef struct {
	uint8_t water_level;
	uint8_t rinse_count;
	uint16_t prewash_shaking_period;
	uint16_t wash_shaking_period;
	uint16_t prewash_time;
	uint16_t prewash_break_time;
	uint16_t break_time;
	uint16_t wash_time;
	uint16_t centrifuge_time;
} wash_cycle_params_t;


bool wash_cycle_start(wash_cycle_params_t* params);
void wash_cycle_pause();
void wash_cycle_resume();
void wash_cycle_skip_step();
void wash_cycle_abort();


#endif /* MAIN_WASH_CYCLE_H_ */
