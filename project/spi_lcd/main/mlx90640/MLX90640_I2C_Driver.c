/**
 * @file MLX90640_I2C_Driver.c
 * @author vik (ufo281@outlook.com)
 * @brief MLX90640 I2C驱动接口
 * @version 0.1
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "MLX90640_I2C_Driver.h"



static inline uint16_t bswap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

/**
 * @brief
 *
 */
void MLX90640_I2CInit(void)
{
    bsp_i2c_init();
}

/**
 * @brief
 *
 * @return int
 */
int MLX90640_I2CGeneralReset(void)
{
    bsp_i2c_deinit();
    return 0;
}

/**
 * @brief
 *
 * @param slaveAddr
 * @param startAddress
 * @param nMemAddressRead
 * @param rx_data_buf
 * @return int
 */
int MLX90640_I2CRead(uint8_t slaveAddr,
                     uint16_t startAddress,
                     uint16_t nMemAddressRead,
                     uint16_t *rx_data_buf)
{
    (void)slaveAddr;

    if (rx_data_buf == NULL) {
        return -1;
    }

    const uint16_t max_words_per_xfer = 32;

    while (nMemAddressRead > 0)
    {
        /*单位是16bit的数据，2byte*/
        uint16_t rx_data_len =
            (nMemAddressRead > max_words_per_xfer)
            ? max_words_per_xfer
            : nMemAddressRead;

        uint8_t tx_buf[2] = {
            (uint8_t)(startAddress >> 8),
            (uint8_t)(startAddress & 0xFF)
        };

        esp_err_t err = i2c_master_transmit_receive(
            bsp_i2cdev_handle,
            tx_buf,
            sizeof(tx_buf),          // 2 bytes 地址
            (uint8_t *)rx_data_buf,
            (size_t)rx_data_len * 2,  // 每个 word = 2 bytes
            I2C_MASTER_TIMEOUT_MS
        );

        if (err != ESP_OK) {
            return -1;
        }

        for (uint16_t i = 0; i < rx_data_len; i++)
        {
            rx_data_buf[i] = bswap16(rx_data_buf[i]);/*大小端数据转换*/
        }

        rx_data_buf += rx_data_len;
        startAddress += rx_data_len;
        nMemAddressRead -= rx_data_len;
    }

    return 0;

}

/**
 * @brief
 *
 * @param slaveAddr
 * @param writeAddress
 * @param wr_data
 * @return int
 */
int MLX90640_I2CWrite(uint8_t slaveAddr,
                      uint16_t writeAddress,
                      uint16_t wr_data)
{
    (void)slaveAddr;

    uint8_t wr_buf[4] = {
        (uint8_t)(writeAddress >> 8),
        (uint8_t)(writeAddress & 0xFF),
        (uint8_t)(wr_data >> 8),
        (uint8_t)(wr_data & 0xFF),
    };

    esp_err_t err = i2c_master_transmit(
        bsp_i2cdev_handle,
        wr_buf,
        sizeof(wr_buf),
        I2C_MASTER_TIMEOUT_MS
    );

    if (err != ESP_OK) {
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(1));

    /*回读检查下写入的有问题吗*/
    uint16_t check_data = 0;
    if (MLX90640_I2CRead(slaveAddr, writeAddress, 1, &check_data) != 0) {
        return -1;
    }
    if (check_data != wr_data) {
        return -2;
    }

    return 0;
}

/**
 * @brief
 *
 * @param freq
 */
void MLX90640_I2CFreqSet(int freq)
{
    (void)freq;
}
