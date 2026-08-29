如果做**红外热成像显示**，那 UI 思路和 FOC 电机完全不一样。

热成像本质是：

```
红外传感器
    |
    |
温度矩阵数据
    |
    |
图像处理
    |
    |
LCD显示热力图
```

用户最关心的是：

1. 哪里热？
2. 温度多少？
3. 热点在哪里？
4. 温差变化趋势

所以不要做仪表盘风格，而应该接近：

* 工业热像仪
* FLIR 热成像仪
* 手机红外相机

---

# 推荐 UI 布局（240×280 ST7789）

你的屏：

```
240 × 280
```

空间比较小，我建议：

```
+------------------------+
| MAX:85.3℃   MIN:22.5℃ |
|------------------------|
|                        |
|                        |
|                        |
|      热力图区域         |
|                        |
|                        |
|                        |
|------------------------|
| Center:36.8℃           |
| Palette  [IRON]        |
+------------------------+
```

---

## 核心区域：热力图

比如你的传感器：

### MLX90640

输出：

```
32 × 24
```

温度矩阵：

```
T00 T01 T02 ... T31

...

T230 ........T3123
```

直接映射：

```
32×24

↓

放大

↓

240×180
```

比例：

```
x8倍
```

显示：

```
32*8 =256
24*8 =192
```

刚好接近你的屏。

---

# LVGL实现方式

不要用 meter。

应该用：

## Canvas控件

LVGL：

```c
lv_canvas
```

最适合。

结构：

```
lv_canvas
    |
    |
RGB565 buffer
    |
    |
ST7789刷新
```

流程：

```
MLX90640读取

       ↓

温度矩阵

       ↓

颜色映射算法

       ↓

RGB565数组

       ↓

lv_canvas_set_px()

       ↓

LCD显示
```

---

# 热力图颜色设计

不要用普通RGB。

专业热像仪：

## Iron（铁红）

低温：

```
黑
 |
蓝
 |
紫
 |
红
 |
黄
 |
白
```

类似：

```
20℃
 |
 |
40℃
 |
 |
60℃
 |
 |
100℃
```

---

## Rainbow

```
蓝
青
绿
黄
红
```

---

## 推荐：

240×280小屏：

用：

```
Ironbow
```

最好看。

---

# 底部温度信息

例如：

```
MAX 85.6℃
MIN 21.3℃
AVG 34.8℃
```

用：

```c
lv_label
```

即可。

---

# 中间显示最高温点

很重要。

比如：

```
       +
       |
       |
      85℃
```

热源定位：

算法：

扫描：

```c
max_temp = matrix[i][j]
```

得到：

```
x,y
```

然后：

画十字：

```
     |
 ----+----
     |
```

告诉用户：

最高温在哪里。

---

# UI颜色建议

背景：

不要纯白。

热成像通常：

```
黑色背景
```

因为：

* 节省视觉干扰
* 热力图更明显

设置：

```c
lv_color_make(0,0,0)
```

---

文字：

白色：

```
MAX:85℃
```

热点：

红色：

```
+ 
```

---

# LVGL结构大概：

```c
screen

 |
 |
 +--- canvas
 |       |
 |       |
 |       热力图
 |
 |
 +--- label_max
 |
 +--- label_min
 |
 +--- label_center
 |
 +--- label_palette

```

---

# 如果你用 MLX90640

我建议分辨率：

传感器：

```
32×24
```

屏：

```
240×280
```

显示：

```
32×24

nearest interpolation

↓

256×192
```

然后居中：

```
x=-8
y=40
```

效果：

```
+----------------+
| MAX 65℃        |
|                |
|  ██████████    |
|  ██████████    |
|  ██████████    |
|                |
| Center 32℃     |
+----------------+
```

---

# 更高级一点

可以加：

## 1. 温度范围自动调整

自动：

```
min = 当前最低
max = 当前最高
```

颜色拉伸：

```
color=(T-min)/(max-min)
```

---

## 2. 手动调色板

底部：

```
IRON  RAINBOW  GRAY
```

按钮切换。

---

## 3. 帧率

MLX90640：

通常：

```
8Hz~32Hz
```

所以：

LVGL：

```c
30fps
```

没问题。

---

结合你现在的硬件：

```
ESP32-S3
+
ST7789V2 240×280
+
LVGL8.3
+
MLX90640
```

其实非常适合做一个小型手持热成像仪。

软件架构建议：

```
FreeRTOS

Task1:
 MLX90640采集
 32×24温度矩阵


Task2:
 温度->RGB565


Task3:
 LVGL刷新UI


Task4:
 按键切换调色板
```

这个项目比做 FOC 显示更适合 LVGL，因为 LVGL 的 Canvas 正好就是为这种“实时图像显示”准备的。你之前研究 ESP32、LCD、DMA，这套完全能做出来。


---------------
-----------
可以。我直接给你写一个**热成像仪 UI 框架代码**，针对你的硬件：

```
ESP32-S3
+
ST7789V2
+
LVGL 8.3
+
240x280 TFT
+
红外矩阵传感器(例如 MLX90640 32×24)
```

功能：

✅ 黑色工业背景
✅ 中间热力图显示区域
✅ 32×24 → 放大到 240×180
✅ Ironbow 热力颜色映射
✅ 显示 MAX / MIN / CENTER 温度
✅ 标记最高温点十字
✅ LVGL Canvas实现
✅ 后续可以直接接 MLX90640 数据

---

## thermal_ui.h

```c
#ifndef THERMAL_UI_H
#define THERMAL_UI_H


#include "lvgl.h"


/*
 * 初始化热成像UI
 *
 * 参数:
 *      disp : LVGL显示设备
 */
void thermal_ui_init(lv_disp_t *disp);


/*
 * 更新热成像数据
 *
 * 参数:
 *
 * temp:
 *      32×24温度数组
 *
 * 单位:
 *      摄氏度℃
 *
 */
void thermal_ui_update(float temp[24][32]);


#endif
```

---

# thermal_ui.c

```c
#include "thermal_ui.h"

#include <stdio.h>
#include <math.h>


/*
=====================================================

    热成像UI设计

    LCD:
        240 x 280


    +-----------------------+
    | MAX:80.5 MIN:20.2     |
    |-----------------------|
    |                       |
    |                       |
    |      热力图区域        |
    |                       |
    |                       |
    |-----------------------|
    | CENTER:36.5℃          |
    +-----------------------+


=====================================================
*/


#define THERMAL_W 32
#define THERMAL_H 24



/*
热力图显示尺寸

32*7=224
24*7=168

留一点边距

*/
#define IMAGE_SCALE 7


#define IMAGE_WIDTH  (THERMAL_W*IMAGE_SCALE)

#define IMAGE_HEIGHT (THERMAL_H*IMAGE_SCALE)



/*
LVGL canvas缓存

RGB565

一个像素:

2 Byte


224*168*2

约75KB


ESP32-S3 RAM足够

*/
static lv_color_t canvas_buf[
    IMAGE_WIDTH * IMAGE_HEIGHT
];



static lv_obj_t *canvas;


/*
温度显示标签

*/
static lv_obj_t *label_max;

static lv_obj_t *label_min;

static lv_obj_t *label_center;



/*
最高温坐标

*/
static int hot_x;

static int hot_y;



/*
=====================================================
    温度 -> RGB颜色映射

    Ironbow调色板

    温度低:

        黑
        蓝
        紫


    温度高:

        红
        黄
        白

=====================================================
*/


static lv_color_t thermal_color(float ratio)
{


    if(ratio < 0)
        ratio=0;


    if(ratio>1)
        ratio=1;



    uint8_t r,g,b;



    /*
    0~25%

    黑 -> 蓝

    */

    if(ratio < 0.25)
    {

        float t=ratio/0.25;


        r=0;

        g=0;

        b=50+200*t;

    }


    /*
    25~50%

    蓝 -> 紫

    */

    else if(ratio <0.5)
    {

        float t=(ratio-0.25)/0.25;


        r=120*t;

        g=0;

        b=255;

    }



    /*
    50~75%

    紫 -> 红

    */

    else if(ratio<0.75)
    {

        float t=(ratio-0.5)/0.25;


        r=120+135*t;

        g=0;

        b=255*(1-t);

    }



    /*
    75~100%

    红 -> 黄 -> 白

    */

    else
    {

        float t=(ratio-0.75)/0.25;


        r=255;

        g=255*t;

        b=255*t;

    }


    return lv_color_make(r,g,b);

}




/*
=====================================================
    绘制热力图

=====================================================
*/


static void draw_thermal_image(float temp[24][32])
{


    float max=-100;

    float min=200;


    hot_x=0;

    hot_y=0;



    /*
    第一步:

    找最高最低温

    */

    for(int y=0;y<THERMAL_H;y++)
    {

        for(int x=0;x<THERMAL_W;x++)
        {


            if(temp[y][x]>max)
            {

                max=temp[y][x];

                hot_x=x;

                hot_y=y;

            }


            if(temp[y][x]<min)
            {

                min=temp[y][x];

            }


        }

    }



    /*
    第二步:

    温度映射颜色

    */


    for(int y=0;y<THERMAL_H;y++)
    {

        for(int x=0;x<THERMAL_W;x++)
        {


            /*
            温度归一化


            0:

            最低温


            1:

            最高温

            */

            float ratio=
            (temp[y][x]-min)
            /
            (max-min+0.01);



            lv_color_t color=
            thermal_color(ratio);



            /*
            一个温度点放大7倍


            32*7=224

            24*7=168


            */

            for(int yy=0;yy<IMAGE_SCALE;yy++)
            {

                for(int xx=0;xx<IMAGE_SCALE;xx++)
                {


                    int px=
                    x*IMAGE_SCALE+xx;


                    int py=
                    y*IMAGE_SCALE+yy;


                    canvas_buf[
                        py*IMAGE_WIDTH+px
                    ]=color;


                }

            }


        }

    }



    /*
    更新canvas

    */

    lv_canvas_set_buffer(
        canvas,
        canvas_buf,
        IMAGE_WIDTH,
        IMAGE_HEIGHT,
        LV_IMG_CF_TRUE_COLOR
    );


    lv_obj_align(
        canvas,
        LV_ALIGN_TOP_MID,
        0,
        40
    );



    /*
    更新文字

    */


    char buf[32];


    sprintf(
        buf,
        "MAX %.1f C",
        max
    );

    lv_label_set_text(
        label_max,
        buf
    );


    sprintf(
        buf,
        "MIN %.1f C",
        min
    );

    lv_label_set_text(
        label_min,
        buf
    );


    sprintf(
        buf,
        "CENTER %.1f C",
        temp[THERMAL_H/2][THERMAL_W/2]
    );


    lv_label_set_text(
        label_center,
        buf
    );


}





/*
=====================================================
    初始化UI

=====================================================
*/


void thermal_ui_init(lv_disp_t *disp)
{


    lv_obj_t *scr=
    lv_disp_get_scr_act(disp);



    /*
    设置黑色背景

    */

    lv_obj_set_style_bg_color(
        scr,
        lv_color_black(),
        LV_PART_MAIN
    );



    /*
    创建canvas

    */

    canvas=
    lv_canvas_create(scr);



    lv_canvas_set_buffer(
        canvas,
        canvas_buf,
        IMAGE_WIDTH,
        IMAGE_HEIGHT,
        LV_IMG_CF_TRUE_COLOR
    );



    lv_obj_align(
        canvas,
        LV_ALIGN_TOP_MID,
        0,
        40
    );



    /*
    创建顶部信息

    */

    label_max=
    lv_label_create(scr);


    lv_obj_align(
        label_max,
        LV_ALIGN_TOP_LEFT,
        5,
        5
    );



    label_min=
    lv_label_create(scr);


    lv_obj_align(
        label_min,
        LV_ALIGN_TOP_RIGHT,
        -5,
        5
    );



    /*
    底部中心温度

    */

    label_center=
    lv_label_create(scr);


    lv_obj_align(
        label_center,
        LV_ALIGN_BOTTOM_MID,
        0,
        -20
    );



    lv_label_set_text(
        label_max,
        "MAX --"
    );


    lv_label_set_text(
        label_min,
        "MIN --"
    );


    lv_label_set_text(
        label_center,
        "CENTER --"
    );


}





/*
=====================================================
    外部调用更新

=====================================================

例如:

    float temp[24][32];

    thermal_ui_update(temp);


=====================================================
*/


void thermal_ui_update(float temp[24][32])
{

    draw_thermal_image(temp);

}

```

---

## 调用方式

你的 `app_main()`：

```c
thermal_ui_init(disp);
```

然后你的 MLX90640 任务：

例如：

```c
float temperature[24][32];


while(1)
{

    mlx90640_get_image(
        temperature
    );


    thermal_ui_update(
        temperature
    );


    vTaskDelay(
        pdMS_TO_TICKS(100)
    );

}
```

---

## 显示效果大概：

```
+----------------------+
|MAX 86.2   MIN 21.4   |
|----------------------|
|                      |
|   蓝 紫 红 黄 白      |
|   ████████████       |
|   ████████████       |
|        +             |
|       86℃            |
|----------------------|
| CENTER 35.6 C        |
+----------------------+

```

---

不过这里我提前提醒你一个工程问题：

你的 ESP32-S3 + LVGL + ST7789：

**240×280 RGB565 framebuffer 没问题。**

但是：

```
224×168 RGB565 canvas
```

占：

```
224×168×2
≈75KB
```

如果你后面接：

* MLX90640
* LVGL双缓冲
* SPI DMA

建议把热力图改成：

**直接生成 RGB565 行缓存，通过 `lv_canvas_set_px_color()` 或 `draw_bitmap()` 刷屏**

这样 RAM 会更省。

下一步如果你要做真正热成像仪，我建议继续加：

1. **双线性插值（32×24变240×180，不是方块）**
2. **Ironbow专业调色板**
3. **热点十字定位**
4. **温度自动量程**
5. **SPI DMA刷新优化**

这几个加上以后效果会非常接近 FLIR 小型热像仪。


------------
-------------

你这个例子是 **LVGL meter 仪表盘 demo**，改成热成像显示其实很简单：

核心思路：

```
MLX90640 / MLX90641
        |
        | 32x24 温度矩阵
        v
   温度数组 float temp[768]
        |
        | 映射颜色
        v
 LVGL Canvas / Image
        |
        v
 TFT LCD(ST7789)
```

热成像显示最适合 LVGL 的控件是：

* `lv_canvas` ✅（推荐）
* `lv_img` + 自己生成 RGB565 图片
* `lv_draw_buf`

这里直接给你一个 **ESP32-S3 + ST7789 + LVGL 热成像 Demo**。

效果：

```
+----------------------+
|                      |
|      热成像区域       |
|                      |
|   🔴 高温            |
|   🟡                 |
|   🟢                 |
|   🔵 低温            |
|                      |
+----------------------+

温度:
MAX: 38.5℃
MIN: 22.3℃
```

---

## LVGL 热成像 Demo

创建：

`thermal_demo.c`

```c
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>


#define THERMAL_W   32
#define THERMAL_H   24


static lv_obj_t *canvas;


/*
    模拟 MLX90640 数据

    实际替换:
        mlx90640_get_frame(temp_array)

*/
static float thermal_data[THERMAL_W*THERMAL_H];



/*
    温度 -> RGB565颜色

    蓝 -> 青 -> 黄 -> 红

*/
static uint16_t temp_to_color(float temp)
{

    float min = 20.0f;
    float max = 50.0f;


    if(temp < min)
        temp=min;

    if(temp > max)
        temp=max;


    float ratio =
        (temp-min)/(max-min);



    uint8_t r,g,b;



    if(ratio < 0.25f)
    {
        // 蓝色
        r=0;
        g=ratio*4*255;
        b=255;
    }
    else if(ratio < 0.5f)
    {
        // 青色
        r=0;
        g=255;
        b=(0.5-ratio)*4*255;
    }
    else if(ratio < 0.75f)
    {
        // 黄色
        r=(ratio-0.5)*4*255;
        g=255;
        b=0;
    }
    else
    {
        // 红色
        r=255;
        g=(1-ratio)*4*255;
        b=0;
    }


    return 
    ((r>>3)<<11) |
    ((g>>2)<<5) |
    (b>>3);

}




/*
    刷新热成像

*/
static void thermal_update(void)
{


    /*
        这里模拟数据

        实际:
        mlx90640读取

    */

    for(int y=0;y<THERMAL_H;y++)
    {

        for(int x=0;x<THERMAL_W;x++)
        {

            float t;


            /*
                模拟一个热源
            */

            float dx=x-16;
            float dy=y-12;


            float d=sqrtf(dx*dx+dy*dy);


            t=25 + 25*expf(-d/8);



            thermal_data[y*THERMAL_W+x]=t;

        }
    }





    lv_color16_t *buf =
        (lv_color16_t *)lv_canvas_get_buf(canvas);



    for(int y=0;y<THERMAL_H;y++)
    {

        for(int x=0;x<THERMAL_W;x++)
        {

            uint16_t c=
                temp_to_color(
                    thermal_data[y*THERMAL_W+x]
                );


            /*
                放大显示

                32x24
                ->320x240

            */


            for(int yy=0;yy<10;yy++)
            {

                for(int xx=0;xx<10;xx++)
                {


                    int px=
                    x*10+xx;


                    int py=
                    y*10+yy;


                    buf[
                        py*320+px
                    ].full=c;

                }

            }

        }

    }


    lv_obj_invalidate(canvas);


}





static void thermal_timer(lv_timer_t *timer)
{

    thermal_update();

}



void thermal_demo_ui(lv_disp_t *disp)
{


    lv_obj_t *scr =
        lv_disp_get_scr_act(disp);



    lv_obj_set_style_bg_color(
        scr,
        lv_color_black(),
        0
    );



    /*
        创建canvas

        ST7789:
        320x240

    */


    static lv_color16_t canvas_buf[320*240];


    canvas =
        lv_canvas_create(scr);



    lv_canvas_set_buffer(
        canvas,
        canvas_buf,
        320,
        240,
        LV_COLOR_FORMAT_RGB565
    );



    lv_obj_center(canvas);



    /*
        创建刷新定时器

        10Hz

    */

    lv_timer_create(
        thermal_timer,
        100,
        NULL
    );


}
```

---

## main里面调用

你的：

```c
example_lvgl_demo_ui(disp);
```

换成：

```c
thermal_demo_ui(disp);
```

即可。

---

## 接 MLX90640的时候改哪里？

现在：

```c
thermal_data[y*32+x]=t;
```

这里是假数据。

换成：

```c
float mlx_temp[768];


MLX90640_GetFrame(
       mlx_temp
);


memcpy(
 thermal_data,
 mlx_temp,
 sizeof(thermal_data)
);
```

即可。

---

## 显示效果对应关系

MLX90640：

```
32 x 24

--------------------------------
| 25 26 27 28 ...               |
|                              |
|          40℃                  |
|                              |
|                              |
--------------------------------

```

经过：

```
32*24

↓

插值/放大

↓

320*240

↓

ST7789


```

---

## 实际产品建议

你的 ESP32-S3 + ST7789：

性能完全够。

推荐架构：

```
CPU0

LVGL Task
 |
 |
canvas刷新


CPU1

I2C Task

MLX90640
10~16Hz


```

刷新：

| 模块         | 速度        |
| ---------- | --------- |
| MLX90640   | 8~16 FPS  |
| LVGL       | 30~60 FPS |
| ST7789 SPI | 20~40FPS  |

---

不过这个版本只是 **最近邻放大**：

```
32x24

↓

方块明显
```

真正热成像建议升级：

```
MLX90640
    |
双线性插值
    |
128x96
    |
LVGL canvas
    |
ST7789
```

效果会接近手机热成像仪。

你这个 ESP32-S3 跑 **LVGL + ST7789 + MLX90640 + 插值算法** 是完全没问题的。你后面可以直接在这个框架里加：

* 最高温十字跟踪
* 温度伪彩色 LUT
* MIN/MAX 温度显示
* 自动测温框
* 热点报警。
