#include <stdbool.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


#ifndef PCA9554_H_
#define PCA9554_H_

typedef struct {
	i2c_master_bus_handle_t i2c_bus_handle;
	SemaphoreHandle_t i2c_bus_mutex;
	uint8_t pin_a0 : 1;
	uint8_t pin_a1 : 1;
	uint8_t pin_a2 : 1;
	bool shared;
} pca9554_config_t;

typedef struct pca9554_control_t *pca9554_handle_t;

esp_err_t pca9554_init(pca9554_config_t* dev_config, pca9554_handle_t* ret_handle);

esp_err_t pca9554A_init(pca9554_config_t* dev_config, pca9554_handle_t* ret_handle);

#define PCA9554_CMD_INPUT_REGISTER 		0x00
#define PCA9554_CMD_OUTPUT_REGISTER 	0x01
#define PCA9554_CMD_POLARITY_REGISTER 	0x02
#define PCA9554_CMD_CONFIG_REGISTER 	0x03
#define PCA9554_CMD_INVALID				0xFF

static inline esp_err_t pca9554_read_input(pca9554_handle_t dev_handle, uint8_t* port_out, int timeout_ms) {
	extern esp_err_t pca9554_read_op(pca9554_handle_t dev_handle, uint8_t* port_out, int timeout_ms, uint8_t cmd);
	return pca9554_read_op(dev_handle, port_out, timeout_ms, PCA9554_CMD_INPUT_REGISTER);
}

esp_err_t pca9554_write_output(pca9554_handle_t dev_handle, uint8_t port_value, int timeout_ms);
esp_err_t pca9554_read_output(pca9554_handle_t dev_handle, uint8_t* port_out, int timeout_ms);
esp_err_t pca9554_output_set_bits(pca9554_handle_t dev_handle, uint8_t bits, int timeout_ms);
esp_err_t pca9554_output_clear_bits(pca9554_handle_t dev_handle, uint8_t bits, int timeout_ms);
esp_err_t pca9554_output_set_clear_bits(pca9554_handle_t dev_handle, uint8_t set_bits, uint8_t clear_bits, int timeout_ms);

static inline esp_err_t pca9554_write_polarity(pca9554_handle_t dev_handle, uint8_t port_value, int timeout_ms) {
	extern esp_err_t pca9554_write_op(pca9554_handle_t dev_handle, uint8_t port_value, int timeout_ms, uint8_t cmd);
	return pca9554_write_op(dev_handle, port_value, timeout_ms, PCA9554_CMD_POLARITY_REGISTER);
}

static inline esp_err_t pca9554_read_polarity(pca9554_handle_t dev_handle, uint8_t* port_out, int timeout_ms) {
	extern esp_err_t pca9554_read_op(pca9554_handle_t dev_handle, uint8_t* port_out, int timeout_ms, uint8_t cmd);
	return pca9554_read_op(dev_handle, port_out, timeout_ms, PCA9554_CMD_POLARITY_REGISTER);
}

/**
 * @brief
 *
 * @param dev_handle
 * @param port_value Configures the directions of the I/O pins, each bit: 0 = output, 1 = input
 * @param xfer_timeout_ms
 * @return
 */
static inline esp_err_t pca9554_write_config(pca9554_handle_t dev_handle, uint8_t port_value, int timeout_ms) {
	extern esp_err_t pca9554_write_op(pca9554_handle_t dev_handle, uint8_t port_value, int timeout_ms, uint8_t cmd);
	return pca9554_write_op(dev_handle, port_value, timeout_ms, PCA9554_CMD_CONFIG_REGISTER);
}

static inline esp_err_t pca9554_read_config(pca9554_handle_t dev_handle, uint8_t* port_out, int timeout_ms) {
	extern esp_err_t pca9554_read_op(pca9554_handle_t dev_handle, uint8_t* port_out, int timeout_ms, uint8_t cmd);
	return pca9554_read_op(dev_handle, port_out, timeout_ms, PCA9554_CMD_CONFIG_REGISTER);
}


#endif /* PCA9554_H_ */
