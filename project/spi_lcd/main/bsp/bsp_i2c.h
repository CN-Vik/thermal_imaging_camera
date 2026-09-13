/**
 * @file bsp_i2c.h
 * @author vik (ufo281@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2026-09-10
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"


#define I2C_MASTER_SCL_IO (6)       /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO (7)       /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM I2C_NUM_0    /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ (750000) /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS 1000

#define MLX90640_I2C_DEV_ADDR (0x33)       /*!< Address of the MLX90640 sensor */


extern i2c_master_bus_handle_t bsp_i2c_handle;
extern i2c_master_dev_handle_t bsp_i2cdev_handle;


esp_err_t bsp_i2c_write_bytes(uint8_t reg_addr,
                              uint8_t *w_data,
                              uint32_t w_data_len);

esp_err_t bsp_i2c_write_1byte(uint8_t reg_addr,
                                  uint8_t data);
esp_err_t bsp_i2c_read_bytes(uint8_t reg_addr,
                             uint8_t *r_data,
                             uint32_t r_len);

void bsp_i2c_init(void);

void bsp_i2c_deinit(void);


#endif