/*
 * main.h
 *
 *  Created on: May 3, 2024
 *      Author: samli
 */

#ifndef MAIN_MAIN_H_
#define MAIN_MAIN_H_

#include "pca9554.h"
#include "esp_task.h"

extern pca9554_handle_t pca_u3_handle, pca_u4_handle, pca_u5_handle;
extern SemaphoreHandle_t i2c_mutex;

#define PCA_IOPANEL pca_u3_handle

#define PCA_VALVES_PRES_SWITCH pca_u4_handle
#define PCA_VALVES PCA_VALVES_PRES_SWITCH
#define PCA_PRES_SWITCH PCA_VALVES_PRES_SWITCH

#define PCA_MOTOR_PUMP pca_u5_handle

#define PCA_PIN_MOTOR_CW	(1 << 0)
#define PCA_PIN_MOTOR_CCW	(1 << 1)
#define PCA_PIN_PUMP		(1 << 2)

#define PCA_PIN_LID					(1 << 0)
#define PCA_PIN_OVERFLOW			(1 << 1)
#define PCA_PIN_PRESS_SWITCH_LV_1	(1 << 2)
#define PCA_PIN_PRESS_SWITCH_LV_2	(1 << 3)

#define PCA_PIN_VALVE_3		(1 << 5)
#define PCA_PIN_VALVE_2		(1 << 6)
#define PCA_PIN_VALVE_1		(1 << 7)

#define PRESSURE_SWITCH_TASK_PRIORITY (ESP_TASK_PRIO_MAX - 5)
#define DEVSTATE_TASK_PRIORITY (ESP_TASK_PRIO_MAX - 5)
#define WASH_CYCLE_TASK_PRIORITY (ESP_TASK_PRIO_MAX - 6)
#define IOPANEL_TASK_PRIORITY (ESP_TASK_PRIO_MAX - 7)

#endif /* MAIN_MAIN_H_ */
