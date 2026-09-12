/**
 * @file bsp_i2c.c
 * @author vik (ufo281@outlook.com)
 * @brief i2c外设使用接口
 *
 * @version 0.1
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "bsp_i2c.h"


static const char *TAG = "bsp_i2c";


i2c_master_bus_handle_t bsp_i2c_handle;
i2c_master_dev_handle_t bsp_i2cdev_handle;




/**
 * @brief i2c master initialization
 */
static void i2c_master_init(i2c_master_bus_handle_t *bsp_i2c_handle, i2c_master_dev_handle_t *bsp_i2cdev_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bsp_i2c_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MLX90640_I2C_DEV_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bsp_i2c_handle, &dev_config, bsp_i2cdev_handle));
}



esp_err_t bsp_i2c_write_bytes(uint8_t reg_addr,
                              uint8_t *w_data,
                              uint32_t w_data_len)
{
    uint8_t write_buf[2] = {reg_addr, 0x00};

    for (int i = 0; i < w_data_len; i++)
    {
        write_buf[1] = w_data[i];
        i2c_master_transmit(bsp_i2cdev_handle,
                            write_buf,
                            sizeof(write_buf),
                            I2C_MASTER_TIMEOUT_MS);
    }
    
    return 0;

}

esp_err_t bsp_i2c_write_1byte(uint8_t reg_addr,
                                  uint8_t data)
{

    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(bsp_i2cdev_handle,
                               write_buf,
                               sizeof(write_buf),
                               I2C_MASTER_TIMEOUT_MS);
}

esp_err_t bsp_i2c_read_bytes(uint8_t reg_addr,
                             uint8_t *r_data,
                             uint32_t r_len)
{
    return i2c_master_transmit_receive(bsp_i2cdev_handle,
                                       &reg_addr, /*write_data_addr*/
                                       1,         /*write data lens*/
                                       r_data,
                                       r_len,
                                       I2C_MASTER_TIMEOUT_MS);
}

void bsp_i2c_init(void)
{
    i2c_master_init(&bsp_i2c_handle, &bsp_i2cdev_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");
}

void bsp_i2c_deinit(void)
{
    /*delete i2c*/
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(bsp_i2cdev_handle));
    ESP_ERROR_CHECK(i2c_del_master_bus(bsp_i2c_handle));
    ESP_LOGI(TAG, "I2C de-initialized successfully");
}