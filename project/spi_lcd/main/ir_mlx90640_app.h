#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 传感器物理点阵
#define MLX_RAW_W               32
#define MLX_RAW_H               24
#define MLX_RAW_PIXELS          (MLX_RAW_W * MLX_RAW_H)

// 兼容旧代码宏
#define MLX_W                   MLX_RAW_W
#define MLX_H                   MLX_RAW_H

// 目标双线性插值高分辨率 (4:3 比例，宽度满屏 240)
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

// 导出高分辨率插值图像显存及 LVGL 图像描述符
extern uint16_t s_display_rgb565[MLX_INTERP_PIXELS];
extern lv_img_dsc_t s_thermal_img_dsc;

// 导出 UI 控件句柄
extern lv_obj_t *img_thermal;
extern lv_obj_t *label_info;

// 声明锁接口与核心任务
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);
void ir_camera_task_main(void *pvParameters);
void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp);

#ifdef __cplusplus
}
#endif