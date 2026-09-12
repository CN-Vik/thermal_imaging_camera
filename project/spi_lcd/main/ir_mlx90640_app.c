/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* i2c - Simple Example

   Simple I2C example that shows how to initialize I2C
   as well as reading and writing from and to registers for a sensor connected over I2C.

   The sensor used in this example is a MLX90640 inertial measurement unit.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "MLX90640_I2C_Driver.h"
#include "MLX90640_API.h"


static const char *TAG = "esp_i2c_basic";


#define FPS2HZ 0x02
#define FPS4HZ 0x03
#define FPS8HZ 0x04
#define FPS16HZ 0x05
#define FPS32HZ 0x06

#define MLX90640_ADDR 0x33
#define RefreshRate FPS32HZ
#define TA_SHIFT 8 // Default shift for MLX90640 in open air

TaskHandle_t ir_task_Handle = NULL;


static uint16_t eeMLX90640[832];
static float mlx90640To[768];
uint16_t frame[834];
float emissivity = 0.95;
int status;



void ir_camera_task_main(void)
{
    MLX90640_I2CInit();

    /* USER CODE BEGIN 2 */
    ESP_LOGI(TAG," 111111111 \n");

    MLX90640_SetRefreshRate(MLX90640_I2C_DEV_ADDR, RefreshRate);
    ESP_LOGI(TAG," 22222 \n");

    MLX90640_SetChessMode(MLX90640_I2C_DEV_ADDR);
    ESP_LOGI(TAG," 33333 \n");

    // paramsMLX90640 mlx90640;
    paramsMLX90640 *mlx90640 = (paramsMLX90640*)calloc(1,sizeof(paramsMLX90640));
    if (mlx90640 == NULL) {
        ESP_LOGE(TAG, "mlx90640 malloc failed!");
        return;
    }
    
    status = MLX90640_DumpEE(MLX90640_I2C_DEV_ADDR, eeMLX90640);
    if (status != 0)
    {
        ESP_LOGI(TAG,"load_system_parameters_error_with_code:%d\r\n", status);
    }
    ESP_LOGI(TAG," 444444 \n");

    status = MLX90640_ExtractParameters(eeMLX90640, mlx90640);
    if (status != 0)
    {
        ESP_LOGI(TAG,"Parameter_extraction_failed_with_error_code:%d\r\n", status);
    }

    ESP_LOGI(TAG," 55555 \n");

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
        ESP_LOGI(TAG,"ir_start\r\n");
        int status = MLX90640_GetFrameData(MLX90640_I2C_DEV_ADDR, frame);
        if (status < 0)
        {
            ESP_LOGI(TAG,"GetFrame_Error: %d\r\n", status);
        }
        float vdd = MLX90640_GetVdd(frame, mlx90640);
        float Ta = MLX90640_GetTa(frame, mlx90640);

        float tr = Ta - TA_SHIFT; // Reflected temperature based on the sensor ambient temperature
        ESP_LOGI(TAG,"vdd:%f,Tr:%f\r\n",vdd,tr);
        MLX90640_CalculateTo(frame, mlx90640, emissivity, tr, mlx90640To);
        ESP_LOGI(TAG,"end\r\n");
        ESP_LOGI(TAG,"\r\n==========================Waveshare==========================\r\n");
        for (int i = 0; i < 768; i++)
        {
            if (i % 32 == 0 && i != 0)
            {
                ESP_LOGI(TAG,"\r\n");
            }
            ESP_LOGI(TAG,"ir_data:%2.2f ", mlx90640To[i]);
        }
        ESP_LOGI(TAG,"\r\n==========================Waveshare==========================\r\n");

        vTaskDelay(pdMS_TO_TICKS(10));

    }

    if (mlx90640)
    {
        free(mlx90640);
        mlx90640 = NULL;
    }
    
    /* USER CODE END 3 */
}
