#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#define MLX_W 32
#define MLX_H 24

// 导出供 LVGL 图像控件直接绑定的显存
extern uint16_t s_raw_rgb565[MLX_W * MLX_H];

// 导出 UI 控件句柄
extern lv_obj_t *img_thermal;
extern lv_obj_t *label_info;

// 声明 app_main.c 中的 LVGL 线程互斥锁
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);

// 声明采集任务与 UI 初始化函数
void ir_camera_task_main(void *pvParameters);
void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp);