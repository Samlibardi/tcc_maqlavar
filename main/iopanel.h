/*
 * iopanel.h
 *
 *  Created on: May 3, 2024
 *      Author: samli
 */

#ifndef MAIN_IOPANEL_H_
#define MAIN_IOPANEL_H_

#include <stdint.h>
#include "esp_event.h"

// hardcoded according to schematic
#define LD1_ROW 3
#define LD1_COL 1
#define LD2_ROW 3
#define LD2_COL 2
#define LD3_ROW 3
#define LD3_COL 3
#define LD4_ROW 3
#define LD4_COL 4
#define LD5_ROW 3
#define LD5_COL 5
#define LD6_ROW 3
#define LD6_COL 6
#define LD7_ROW 2
#define LD7_COL 6
#define LD8_ROW 2
#define LD8_COL 5
#define LD9_ROW 2
#define LD9_COL 4
#define LD10_ROW 2
#define LD10_COL 3
#define LD11_ROW 2
#define LD11_COL 2
#define LD12_ROW 1
#define LD12_COL 0
#define LD13_ROW 2
#define LD13_COL 0
#define LD14_ROW 2
#define LD14_COL 1
#define LD15_ROW 1
#define LD15_COL 2
#define LD16_ROW 1
#define LD16_COL 1
#define LD17_ROW 1
#define LD17_COL 3

#define DEF_LED_ENUM(id, ldnum) id = (1 << (8 * ldnum ## _ROW + ldnum ## _COL))

typedef enum {
	IOPANEL_LED_NONE = 0,
	DEF_LED_ENUM(IOPANEL_LED_WATER_LEVEL_LOW, LD1),
	DEF_LED_ENUM(IOPANEL_LED_WATER_LEVEL_MID, LD2),
	DEF_LED_ENUM(IOPANEL_LED_WATER_LEVEL_HIGH, LD3),
	DEF_LED_ENUM(IOPANEL_LED_CLOTHING_TYPE_DELICATE, LD6),
	DEF_LED_ENUM(IOPANEL_LED_CLOTHING_TYPE_COLOR, LD5),
	DEF_LED_ENUM(IOPANEL_LED_CLOTHING_TYPE_WHITE, LD4),
	DEF_LED_ENUM(IOPANEL_LED_PROGRAM_PREWASH, LD7),
	DEF_LED_ENUM(IOPANEL_LED_PROGRAM_BREAK, LD8),
	DEF_LED_ENUM(IOPANEL_LED_PROGRAM_WASH, LD9),
	DEF_LED_ENUM(IOPANEL_LED_PROGRAM_RINSE, LD10),
	DEF_LED_ENUM(IOPANEL_LED_PROGRAM_CENTRIFUGE, LD11),
	DEF_LED_ENUM(IOPANEL_LED_RINSE_COUNT_1, LD12),
	DEF_LED_ENUM(IOPANEL_LED_RINSE_COUNT_2, LD13),
	DEF_LED_ENUM(IOPANEL_LED_RINSE_COUNT_3, LD14),
	DEF_LED_ENUM(IOPANEL_LED_BREAK_DURATION_SHORT, LD15),
	DEF_LED_ENUM(IOPANEL_LED_BREAK_DURATION_LONG, LD16),
	DEF_LED_ENUM(IOPANEL_LED_ONOFF, LD17),
} iopanel_led_t;

#define IOPANEL_LEDS_WATER_LEVEL_ALL (IOPANEL_LED_WATER_LEVEL_LOW | IOPANEL_LED_WATER_LEVEL_MID | IOPANEL_LED_WATER_LEVEL_HIGH)
#define IOPANEL_LEDS_CLOTHING_TYPE_ALL (IOPANEL_LED_CLOTHING_TYPE_DELICATE | IOPANEL_LED_CLOTHING_TYPE_COLOR | IOPANEL_LED_CLOTHING_TYPE_WHITE)
#define IOPANEL_LEDS_PROGRAM_ALL (IOPANEL_LED_PROGRAM_PREWASH | IOPANEL_LED_PROGRAM_BREAK | IOPANEL_LED_PROGRAM_WASH | IOPANEL_LED_PROGRAM_RINSE | IOPANEL_LED_PROGRAM_CENTRIFUGE)
#define IOPANEL_LEDS_RINSE_COUNT_ALL (IOPANEL_LED_RINSE_COUNT_1 | IOPANEL_LED_RINSE_COUNT_2 | IOPANEL_LED_RINSE_COUNT_3)
#define IOPANEL_LEDS_BREAK_DURATION_ALL (IOPANEL_LED_BREAK_DURATION_SHORT | IOPANEL_LED_BREAK_DURATION_LONG)
#define IOPANEL_LEDS_ALL (IOPANEL_LEDS_WATER_LEVEL_ALL | IOPANEL_LEDS_CLOTHING_TYPE_ALL | IOPANEL_LEDS_PROGRAM_ALL | IOPANEL_LEDS_RINSE_COUNT_ALL | IOPANEL_LEDS_BREAK_DURATION_ALL | IOPANEL_LED_ONOFF)


ESP_EVENT_DECLARE_BASE(IOPANEL_EVENT);

typedef enum {
	IOPANEL_EVENT_BUTTON_ONOFF,
	IOPANEL_EVENT_BUTTON_WATER_LEVEL,
	IOPANEL_EVENT_BUTTON_CLOTHING_TYPE,
	IOPANEL_EVENT_BUTTON_PROGRAM,
	IOPANEL_EVENT_BUTTON_RINSE_COUNT,
	IOPANEL_EVENT_BUTTON_BREAK_DURATION,
	IOPANEL_EVENT_BUTTON_START_PAUSE,
} iopanel_event_t;

void __attribute__((noreturn)) iopanel_task_entry(void* params);

static inline void iopanel_set_leds(uint32_t leds_set) {
	extern volatile uint32_t iopanel_led_cols;
	iopanel_led_cols = iopanel_led_cols | leds_set;
}

static inline void iopanel_clear_leds(uint32_t leds_clear) {
	extern volatile uint32_t iopanel_led_cols;
	iopanel_led_cols = iopanel_led_cols & ~leds_clear;
}

static inline void iopanel_set_clear_leds(uint32_t leds_set, uint32_t leds_clear) {
	extern volatile uint32_t iopanel_led_cols;
	iopanel_led_cols = (iopanel_led_cols & ~leds_clear) | leds_set;
}

static inline void iopanel_toggle_leds(uint32_t leds_toggle) {
	extern volatile uint32_t iopanel_led_cols;
	uint32_t tmp_leds = iopanel_led_cols;
	iopanel_led_cols = (tmp_leds & ~leds_toggle) | (~tmp_leds & leds_toggle);
}

#endif /* MAIN_IOPANEL_H_ */
