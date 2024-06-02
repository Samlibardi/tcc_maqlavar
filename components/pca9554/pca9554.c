#include "pca9554.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct pca9554_control_t{
	i2c_master_dev_handle_t i2c_dev_handle;
	uint8_t last_cmd;
	uint8_t output_reg_cache;
	bool shared : 1;
	StaticSemaphore_t dev_mutex;
	SemaphoreHandle_t i2c_bus_mutex;
} pca9554_control_t;

#define PCA9554_BASE_ADDR 0x20
#define PCA9554A_BASE_ADDR 0x38
#define PCA9554_MAKE_ADDR(base_addr, A0, A1, A2) (base_addr | (A2 ? (1 << 2) : 0) | (A1 ? (1 << 1) : 0) | (A0 ? (1 << 0) : 0))


#define PCA9554_OUTPUT_REG_DEFAULT 0xFF

#define TAG "pca9554"

static esp_err_t pca9554x_init(pca9554_config_t* dev_config, pca9554_handle_t* ret_handle, uint32_t base_addr) {
    esp_err_t ret = ESP_OK;

	i2c_device_config_t i2c_dev_config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = PCA9554_MAKE_ADDR(base_addr, dev_config->pin_a0, dev_config->pin_a1, dev_config->pin_a2),
		.scl_speed_hz = 400000,
	};

	i2c_master_dev_handle_t i2c_dev_handle;

	xSemaphoreTake(dev_config->i2c_bus_mutex, portMAX_DELAY);
	ret = i2c_master_bus_add_device(dev_config->i2c_bus_handle, &i2c_dev_config, &i2c_dev_handle);
	xSemaphoreGive(dev_config->i2c_bus_mutex);

	if(ret == ESP_OK) {
		pca9554_control_t* cb = heap_caps_calloc(1, sizeof(pca9554_control_t), MALLOC_CAP_DEFAULT);
		if(cb == NULL) {
			ESP_LOGE(TAG, "%s(%d): no memory for PCA9554 device control block", __FUNCTION__, __LINE__);
			xSemaphoreTake(dev_config->i2c_bus_mutex, portMAX_DELAY);
			i2c_master_bus_rm_device(i2c_dev_handle);
			xSemaphoreGive(dev_config->i2c_bus_mutex);
			ret = ESP_ERR_NO_MEM;
		}
		else {
			cb->i2c_dev_handle = i2c_dev_handle;
			cb->i2c_bus_mutex = dev_config->i2c_bus_mutex;
			cb->shared = dev_config->shared;
			cb->last_cmd = PCA9554_CMD_INVALID;
			cb->output_reg_cache = PCA9554_OUTPUT_REG_DEFAULT;

		    xSemaphoreCreateRecursiveMutexStatic(&cb->dev_mutex);

			*ret_handle = cb;
		}
	}

	return ret;
}


esp_err_t pca9554_init(pca9554_config_t* dev_config, pca9554_handle_t* ret_handle) {
	return pca9554x_init(dev_config, ret_handle, PCA9554_BASE_ADDR);
}

esp_err_t pca9554A_init(pca9554_config_t* dev_config, pca9554_handle_t* ret_handle) {
	return pca9554x_init(dev_config, ret_handle, PCA9554A_BASE_ADDR);
}

esp_err_t pca9554_read_op(pca9554_handle_t dev_handle, uint8_t* port_out, int timeout_ms, uint8_t cmd) {
    esp_err_t ret = ESP_OK;

    ret = (xSemaphoreTakeRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex, timeout_ms) == pdPASS) ? ESP_OK : ESP_ERR_TIMEOUT;

    if(ret == ESP_OK) {
		if(dev_handle->shared || dev_handle->last_cmd != cmd) {
			uint8_t wr_buf[] = { cmd };

			if(xSemaphoreTake(dev_handle->i2c_bus_mutex, timeout_ms) == pdPASS) {
				ret = i2c_master_transmit_receive(dev_handle->i2c_dev_handle, wr_buf, sizeof(wr_buf), port_out, 1, timeout_ms);
				xSemaphoreGive(dev_handle->i2c_bus_mutex);
			} else ret = ESP_ERR_TIMEOUT;
			dev_handle->last_cmd = (ret == ESP_OK) ? cmd : PCA9554_CMD_INVALID;
		} else {
			if(xSemaphoreTake(dev_handle->i2c_bus_mutex, timeout_ms) == pdPASS) {
				ret = i2c_master_receive(dev_handle->i2c_dev_handle, port_out, 1, timeout_ms);
				xSemaphoreGive(dev_handle->i2c_bus_mutex);
			} else ret = ESP_ERR_TIMEOUT;
		}
		xSemaphoreGiveRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex);
    }

	return ret;
}

esp_err_t pca9554_write_op(pca9554_handle_t dev_handle, uint8_t port_value, int timeout_ms, uint8_t cmd) {
    esp_err_t ret = ESP_OK;

    ret = (xSemaphoreTakeRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex, timeout_ms) == pdPASS) ? ESP_OK : ESP_ERR_TIMEOUT;

    if(ret == ESP_OK) {
		uint8_t buf[] = {cmd , port_value };

		if(xSemaphoreTake(dev_handle->i2c_bus_mutex, timeout_ms) == pdPASS) {
			ret = i2c_master_transmit(dev_handle->i2c_dev_handle, buf, sizeof(buf), timeout_ms);
			xSemaphoreGive(dev_handle->i2c_bus_mutex);
		} else ret = ESP_ERR_TIMEOUT;
		dev_handle->last_cmd = (ret == ESP_OK) ? cmd : PCA9554_CMD_INVALID;
		xSemaphoreGiveRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex);
    }

	return ret;
}

esp_err_t pca9554_write_output(pca9554_handle_t dev_handle, uint8_t port_value, int timeout_ms) {
    esp_err_t ret = ESP_OK;

    ret = (xSemaphoreTakeRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex, timeout_ms) == pdPASS) ? ESP_OK : ESP_ERR_TIMEOUT;

    if(ret == ESP_OK) {
		if(!dev_handle->shared && dev_handle->output_reg_cache == port_value)
			ret = ESP_OK;
		else {
			ret = pca9554_write_op(dev_handle, port_value, timeout_ms, PCA9554_CMD_OUTPUT_REGISTER);
			if(ret == ESP_OK)
				dev_handle->output_reg_cache = port_value;
		}
		xSemaphoreGiveRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex);
    }

	return ret;
}

esp_err_t pca9554_read_output(pca9554_handle_t dev_handle, uint8_t* port_out, int timeout_ms) {
    esp_err_t ret = ESP_OK;

    ret = (xSemaphoreTakeRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex, timeout_ms) == pdPASS) ? ESP_OK : ESP_ERR_TIMEOUT;

    if(ret == ESP_OK) {
		if(!dev_handle->shared) {
			*port_out = dev_handle->output_reg_cache;
			ret = ESP_OK;
		}
		else {
			ret = pca9554_read_op(dev_handle, &dev_handle->output_reg_cache, timeout_ms, PCA9554_CMD_OUTPUT_REGISTER);
			if(ret == ESP_OK)
				*port_out = dev_handle->output_reg_cache;
		}
		xSemaphoreGiveRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex);
    }

	return ret;
}

esp_err_t pca9554_output_set_bits(pca9554_handle_t dev_handle, uint8_t bits, int timeout_ms) {
    esp_err_t ret = ESP_OK;

    ret = (xSemaphoreTakeRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex, timeout_ms) == pdPASS) ? ESP_OK : ESP_ERR_TIMEOUT;

    if(ret == ESP_OK) {
    	uint8_t reg;
		if(dev_handle->shared) {
			ret = pca9554_read_output(dev_handle, &reg, timeout_ms);
		}
		else
			reg = dev_handle->output_reg_cache;

		if(ret == ESP_OK) {
			ret = pca9554_write_output(dev_handle, reg | bits, timeout_ms);
		}
		xSemaphoreGiveRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex);
    }

	return ret;
}

esp_err_t pca9554_output_clear_bits(pca9554_handle_t dev_handle, uint8_t bits, int timeout_ms) {
    esp_err_t ret = ESP_OK;

    ret = (xSemaphoreTakeRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex, timeout_ms) == pdPASS) ? ESP_OK : ESP_ERR_TIMEOUT;

    if(ret == ESP_OK) {
		uint8_t reg;
		if(dev_handle->shared) {
			ret = pca9554_read_output(dev_handle, &reg, timeout_ms);
		}
		else
			reg = dev_handle->output_reg_cache;

		if(ret == ESP_OK) {
			ret = pca9554_write_output(dev_handle, reg & ~bits, timeout_ms);
		}
		xSemaphoreGiveRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex);
    }

	return ret;
}

esp_err_t pca9554_output_set_clear_bits(pca9554_handle_t dev_handle, uint8_t set_bits, uint8_t clear_bits, int timeout_ms) {
    esp_err_t ret = ESP_OK;

    ret = (xSemaphoreTakeRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex, timeout_ms) == pdPASS) ? ESP_OK : ESP_ERR_TIMEOUT;

    if(ret == ESP_OK) {
		uint8_t reg;
		if(dev_handle->shared) {
			ret = pca9554_read_output(dev_handle, &reg, timeout_ms);
		}
		else
			reg = dev_handle->output_reg_cache;

		if(ret == ESP_OK) {
			ret = pca9554_write_output(dev_handle, (reg & ~clear_bits) | set_bits, timeout_ms);
		}
		xSemaphoreGiveRecursive((SemaphoreHandle_t)&dev_handle->dev_mutex);
    }

	return ret;
}
