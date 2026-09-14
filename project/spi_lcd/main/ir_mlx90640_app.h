#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MLX_RAW_W               32
#define MLX_RAW_H               24
#define MLX_RAW_PIXELS          (MLX_RAW_W * MLX_RAW_H)

#define MLX_W                   MLX_RAW_W
#define MLX_H                   MLX_RAW_H

#define MLX_INTERP_W            240
#define MLX_INTERP_H            180
#define MLX_INTERP_PIXELS       (MLX_INTERP_W * MLX_INTERP_H)

#define MLX90640_ADDR           0x33
#define TA_SHIFT                8

#define FPS2HZ                  0x02
#define FPS4HZ                  0x03
#define FPS8HZ                  0x04
#define FPS16HZ                 0x05
#define FPS32HZ                 0x06
#define RefreshRate             FPS32HZ 

// 导出图像描述符与控件句柄
extern lv_img_dsc_t s_thermal_img_dsc;
extern lv_obj_t *img_thermal;
extern lv_obj_t *label_info;

// 声明关键函数
esp_err_t ir_camera_app_init(void); // 核心：前置显存初始化
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);
void ir_camera_task_main(void *pvParameters);
void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp);

#ifdef __cplusplus
}
#endif