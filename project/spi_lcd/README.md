> 📖 **项目架构与新人上手指南（推荐先读）**：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

我看了你这个完整 `ir_camera_task_main()`，这已经不是简单的 MLX90640 驱动了，而是一套比较完整的 **ESP32-S3 热成像图像处理 Pipeline（热像仪算法链）**。

整体流程非常清晰，我给你拆一下：

---

# 一、整体数据流

你的代码实际流程：

```
             MLX90640
                |
                |
        I2C读取768像素
                |
                |
        官方温度计算
 MLX90640_CalculateTo()
                |
                |
        坏点修复
 BadPixelsCorrection()
                |
                |
        Subpage棋盘格修正
                |
                |
        时间滤波
 filtered_temp
                |
                |
        空间滤波
 spatial_filter_natural()
                |
                |
        温度范围映射
 20~35℃ ---> 0~255
                |
                |
        双线性插值
 32×24 ---> 240×240
                |
                |
        Ironbow伪彩色
                |
                |
        LVGL显示
                |
              LCD
```

这个已经接近一个低端手持热像仪的软件架构。

---

# 二、每一步作用分析

---

# 1. MLX90640原始采集

这里：

```c
MLX90640_GetFrameData()
```

拿到：

```
834 uint16_t
```

其中：

```
0~831
```

是数据。

```
832
```

控制信息。

```
833
```

subpage。

---

MLX90640不是一次输出768点。

它是：

## Chess模式

两个subpage：

```
Subpage 0

● ○ ● ○
○ ● ○ ●
● ○ ● ○


Subpage 1

○ ● ○ ●
● ○ ● ○
○ ● ○ ●
```

两次合起来：

```
32×24=768
```

所以你这里：

```c
subpage_init[2]
```

处理正确。

---

# 2. 官方温度计算

这里：

```c
MLX90640_CalculateTo(
    ir_frame_data,
    mlx_params,
    emissivity,
    ta - TA_SHIFT,
    raw_temp_matrix
);
```

这是核心。

输入：

- ADC红外数据
- EEPROM校准参数
- 环境温度
- 发射率


输出：

```
raw_temp_matrix[]
```

单位：

℃


例如：

```
25.4
25.6
36.7
```

---

# 3. 坏点校正

这里：

```c
MLX90640_BadPixelsCorrection()
```

非常重要。

MLX90640有：

- broken pixels
- outlier pixels


例如：

坏点：

```
25 25 25
25 80 25
25 25 25
```

修正：

```
25 25 25
25 25 25
25 25 25
```


工业热像仪必做。

---

# 4. Subpage棋盘失配滤波

你的这段：

```c
if(diff >0.8)
```

这是针对 MLX90640 特有问题。


因为：

两个subpage：

```
A:
●○●○

B:
○●○●
```

两个采样时间不同。


运动场景：

可能：

A:

```
人体25℃
```

B:

```
人体26℃
```


合成后：

出现棋盘：

```
25 26 25 26

26 25 26 25
```


你这里：

比较邻居：

```c
avg
```

发现差异：

```c
diff >0.8℃
```

然后融合。

这个思路不错。


---

# 5. 时间滤波（Temporal Filter）


这里：

```c
filtered_temp[i] =
alpha*raw+
(1-alpha)*old
```


这是：

指数移动平均 EMA。


但是你用了自适应：

## 小变化

```
d <0.5℃
```

alpha:

```c
0.12
```

强滤波。


效果：

```
25.1
25.2
25.1
25.2
```

变稳定。


---

## 大变化

例如手靠近：

```
25℃
→
36℃
```


d:

>1.5


alpha:

```
0.8
```


响应快。


这个设计比固定EMA好。

---

# 6. 空间滤波

这里就是你前面问的：

```c
spatial_filter_natural()
```


作用：

降低空间噪声。


输入：

```
32×24
```

输出：

```
32×24
```


注意：

它不是插值。

只是平滑。

---

# 7. 温度转8bit


这里：

```c
temperature_to_norm_u8()
```


你的：

```c
20℃~35℃
```

映射：

```
20℃

|
0


35℃

|
255
```


例如：

30℃：

```
(30-20)/(35-20)*255

=170
```


得到：

```
0~255
```

给颜色查表。


---

这个设计很关键。


很多人喜欢：

每帧：

```
min=23
max=37
```

然后自动拉伸。


结果：

画面：

一直呼吸：

红蓝变化。


你固定范围：

更像真实产品。

---

# 8. Ironbow LUT


这里：

```c
s_ironbow_lut[256]
```


就是：

温度值:

```
0~255
```

映射：

RGB565。


比如：

冷：

```
蓝
```

中：

```
红
```

热：

```
黄白
```


LCD直接显示。

---

# 9. 双线性插值


这里：

```c
bilinear_scale_fullscreen_1to1()
```


非常关键。


MLX90640：

```
32×24
```


LCD:

```
240×240
```


直接放：

很难看。


所以：

插值：

```
32×24

↓

240×240
```


类似：

Photoshop放大图片。


---

# 10. 双缓冲


这里：

```c
s_display_buf[2]
```


这是LVGL显示优化。


避免：

正在刷屏时：

CPU修改buffer。


结构：

```
CPU绘制

buffer A


LCD读取

buffer B


交换
```

---

# 三、你的算法等级

我给评价：

|模块|水平|
|-|-|
|官方驱动|正确|
|坏点校正|有|
|Subpage处理|有|
|时间滤波|优秀|
|空间滤波|基础|
|固定温区映射|正确|
|双线性插值|有|
|伪彩色|有|
|LVGL显示|有|
|双缓存|有|

整体：

⭐⭐⭐⭐☆


已经超过很多DIY项目。

---

# 四、但是还有几个明显不足

如果目标是接近 FLIR / 海曼：

还缺：

---

## 1. NUC非均匀校正

现在：

```
每个像素独立温度
```

但是MLX90640：

不同像素：

存在固定偏差。


例如：

```
pixel1 永远 +0.8℃
pixel2 永远 -0.5℃
```


需要：

offset correction。


---

## 2. 边缘增强


现在：

滤波后：

会糊。


增加：

Unsharp Mask:

```
sharp =
original+(original-blur)*k
```


人体轮廓明显。


---

## 3. 坏帧检测


比如：

I2C异常：

温度：

```
-40℃
```

现在：

主要靠：

```
val >1
val<200
```


可以增加：

CRC / frame validity。


---

## 4. 自动温区


你现在：

```c
20~35℃
```

室内很好。


但是：

室外：

-20℃
或者

发动机：

100℃


不适合。


产品一般：

固定模式+自动模式。


---

# 五、如果这是你的 ESP32-S3 热成像项目

我觉得方向非常对。

你的硬件：

ESP32-S3
+
ST7789
+
MLX90640


这个代码已经接近：

```
Seek Compact
低端模式
```

但是：

ESP32-S3还能继续优化：

建议增加：

```
MLX90640
 |
NUC
 |
Bad pixel
 |
Temporal filter
 |
Spatial filter
 |
Edge enhancement
 |
Bilinear
 |
Pseudo color
 |
LVGL
```

---

另外我注意到一个地方：

你现在：

```c
float *raw_temp_matrix
float *filtered_temp
float *spatial_temp
```

三个：

768 float


内存：

约：

```
768*4*3
=
9.2KB
```

没问题。

ESP32-S3完全可以。


---

总体评价：

**这个代码不是简单demo，是一个完整热像仪显示算法框架。**

如果后续升级到：

- MLX90642（32×24但是更快）
- Heimann 32×32 / 80×64
- FLIR Lepton 160×120
- RV1106双光融合

这个架构基本还能复用。你现在缺的主要是 **NUC校准 + 边缘增强 + RGB融合**。你之前说想做双光融合，这个代码可以作为热通道部分。



--------------
--------------
# 热成像相机固件 · 架构与上手指南

> 目标读者：第一次接触本项目的嵌入式/软件新人。
> 读完本文你应该能回答：**东西是怎么跑起来的、每份代码在干什么、我想改某个东西该动哪个文件。**
>
> 项目路径：`project/spi_lcd`　｜　芯片：ESP32-S3　｜　框架：ESP-IDF 5.3.5 + LVGL 8.3.11

---

## 0. 这份文档怎么读

| 你想干的事 | 直接跳到 |
|---|---|
| 先跑起来看看 | [第 12 章 构建与烧录](#12-构建烧录与调试) |
| 搞清楚整体结构 | [第 3 章 系统架构](#3-系统架构) |
| 搞懂热像图是怎么算出来的 | [第 5 章 热成像数据流水线](#5-热成像数据流水线核心) |
| 改界面 / 加设置项 | [第 7 章 UI 子系统](#7-ui-子系统) |
| 改默认设置值、加语言、加文案 | [第 8 章](#8-配置持久化nvs) / [第 9 章](#9-多语言与字体) |
| 踩坑之前先看雷区 | [第 14 章 已知坑](#14-已知坑踩过的雷) |

---

## 1. 项目是什么

### 1.1 功能清单

一台**手持式红外热成像相机**的完整固件：

- **实时热像**：MLX90640 32×24 红外阵列 → 双线性插值放大到 280×240 → 伪彩色 → 屏幕
- **9 种伪彩**：白热 / 黑热 / 彩虹 / 铁红 / 高热红 / 熔岩 / 极地 / 绿热 / 冷热双警示
- **测温**：全画面最高温 / 最低温 / 中心点（或自定义定点）温度，带十字标记与气泡标签
- **温度曲线**：最近 30 秒采样趋势图，可选采样源（中心 / 定点 / 最高 / 最低），显示 X 时间轴 + Y 温度轴刻度
- **相册**：拍照存归一化温度矩阵（每张约 800 B），可浏览、左右滑动切换、单张删除、全部删除，并显示存储容量条
- **全屏设置页**：15 个页面（色彩样式、温度标签、曲线、旋转、镜像、温度单位、屏幕、滤波、发射率、伽马、校准、语言、恢复出厂、关于、关机）
- **5 种语言**：中文 / English / Русский / 日本語 / 한국어，首次开机引导选择
- **电源管理**：软件电源自锁、背光 PWM 调光、自动休眠（关背光）、软件关机
- **触摸交互**：长按呼出设置、左右滑动切配色、双击拍照、点按设定定点

### 1.2 硬件平台

| 部件 | 型号 / 规格 | 总线 | 引脚 |
|---|---|---|---|
| MCU | ESP32-S3（QFN56, rev v0.2, 240 MHz, 4 MB Flash, 2 MB PSRAM） | — | — |
| 显示屏 | ST7789V2，物理 **240×280** IPS（竖屏面板） | SPI2 @ 40 MHz | SCLK **13**、MOSI **12**、CS **14**、DC **17**、RST **9**、背光 **18** |
| 触摸 | CST816S 电容触摸 | I2C1 @ 默认速率 | SDA **10**、SCL **11**、INT **8**（RST 与屏共用 9，驱动填 -1） |
| 红外传感器 | MLX90640，32×24 = 768 像素，地址 0x33 | I2C0 @ **700 kHz** | SDA **6**、SCL **7** |
| 电源自锁 | 拉高保持供电，拉低断电 | GPIO 输出 | **36** |
| 按键检测 | 输入 + 内部上拉，按下为 0 | GPIO 输入 | **37** |

> 引脚定义全部集中在 [`../main/app_main.c`](../main/app_main.c) 顶部宏（`EXAMPLE_PIN_NUM_*`），
> 电源脚在 [`../main/dev_sys.c`](../main/dev_sys.c) 里另行定义了一份（`PIN_PWR_HOLD` / `PIN_KEY_DET`），**改引脚要两边一起改**。

### 1.3 软件栈与版本

| 组件 | 版本 | 说明 |
|---|---|---|
| ESP-IDF | v5.3.5 | 目标 `esp32s3` |
| LVGL | 8.3.11 | 通过组件管理器引入（`idf_component.yml`） |
| esp_lcd_touch | 1.2.1 | 触摸抽象层 |
| esp_lcd_touch_cst816s | 1.1.2 | CST816S 驱动 |
| FreeRTOS | IDF 内置 | tick = **100 Hz** |
| MLX90640 官方驱动 | 源码内置 | [`../main/mlx90640/`](../main/mlx90640/) |

---

## 2. 目录与文件地图

### 2.1 顶层

```
project/spi_lcd/
├── CMakeLists.txt          # 顶层工程，只有 3 行：引入 IDF project.cmake，定义工程名 spi_lcd
├── partitions.csv          # ★ 分区表（见下表）
├── sdkconfig               # 当前生效配置（menuconfig 生成，可被 build 覆盖）
├── sdkconfig.defaults      # ★ 默认配置，改这里才会在重编时生效（带注释说明关键项）
├── dependencies.lock       # 组件管理器锁文件
├── _gen_fonts.py           # ★ 多语言字体 + 字符串表 生成脚本（改文案必读）
├── main/                   # 全部应用代码
├── docs/                   # 文档
├── tools/                  # 开机图制作 / 图片预览等一次性脚本（不参与编译）
├── build/                  # 构建产物（不用管）
└── managed_components/     # 组件管理器拉下来的 LVGL 等源码（不要改）
```

**分区表** [`../partitions.csv`](../partitions.csv)：

| 名称 | 类型 | 偏移 | 大小 | 用途 |
|---|---|---|---|---|
| `nvs` | data/nvs | 0x9000 | 24 KB | 所有设置、语言、曲线快照 |
| `phy_init` | data/phy | 0xF000 | 4 KB | 射频校准（本项目没用到 WiFi） |
| `factory` | app/factory | 0x10000 | **1 MB** | 固件本体（当前约 743 KB，剩 29%） |
| `storage` | data/spiffs | 0x110000 | **2 MB** | 照片存储（借 spiffs 类型做裸扇区读写，**没有挂文件系统**） |

### 2.2 `main/` 逐文件说明

按重要性排序：

| 文件 | 行数量级 | 职责 |
|---|---|---|
| **`ir_mlx90640_app.c/.h`** | ~1400 | ★ **核心**。MLX90640 采集任务、整套图像处理流水线、色彩 LUT、显示坐标变换、设置项 NVS 读写、温度单位换算、曲线环形缓冲 |
| **`ir_settings_ui.c/.h`** | ~1590 | ★ **全屏设置页**。15 个子页面、菜单列表、返回栏、背光 PWM、自动休眠、曲线图表刷新 |
| **`lvgl_demo_ui.c`** | ~1450 | ★ **主界面 HUD** + 手势（长按/滑动/双击）+ 相册 + 照片查看器 + Toast + 遥控预览菜单（历史遗留） |
| **`app_main.c`** | ~500 | ★ 入口。SPI/I2C 初始化、ST7789 面板、LVGL 移植（flush/touch/tick/task/mutex）、创建采集任务 |
| `photo.c/.h` | ~180 | 照片存储：分区裸扇区读写、槽位管理、容量查询 |
| `ir_color_modes.c/.h` | ~360 | 9 种伪彩色映射函数 + 256 级 LUT 生成 + 模式名 |
| `lang.c/.h` | ~110 | 语言状态与 NVS 持久化、字体选择 |
| `lang_data.c` | 生成文件 | 5 语言字符串表（由 `_gen_fonts.py` 生成，**勿手改**） |
| `app_font_zh/ja/ko/ru.c` | 生成文件 | 4 套字体子集，16 px，4 bpp（由 `_gen_fonts.py` 生成，**勿手改**） |
| `boot_ui.c/.h` | ~200 | 开机 Logo（1.5 s）+ 首次开机语言选择（阻塞） |
| `power_on_start_img.c` | 图片数组 | 280×240 开机图 RGB565 C 数组 |
| `dev_sys.c/.h` | ~130 | 电源自锁、软件关机、自动关机定时器、重启 |
| `bsp/bsp_i2c.c/.h` | ~120 | MLX90640 用的 I2C0 总线封装（读写字节） |
| `mlx90640/MLX90640_API.c` | 官方 | 传感器寄存器/EEPROM/温度计算/坏点校正 |
| `mlx90640/MLX90640_I2C_Driver.c` | 官方 | 传感器 I2C 底层（调用 `bsp_i2c`） |

> **注意**：`lang_data.c` 和 4 个 `app_font_*.c` 都是 `_gen_fonts.py` 的产物，手改会在下次生成时被覆盖；
> 而且它们只包含「字符串表里出现过的字符」，见 [9.2](#92-字体子集生成改文案必读)。

---

## 3. 系统架构

### 3.1 分层图

```
┌──────────────────────────────────────────────────────────────┐
│  应用层 UI                                                    │
│  ├─ ir_settings_ui.c   全屏设置页（15 页）                     │
│  ├─ lvgl_demo_ui.c     主界面 HUD / 手势 / 相册 / 查看器        │
│  ├─ boot_ui.c          开机图 / 首次语言选择                   │
│  └─ photo.c            相册存储                                │
├──────────────────────────────────────────────────────────────┤
│  业务/算法层                                                   │
│  ├─ ir_mlx90640_app.c  采集任务 + 图像处理流水线 + 设置持久化    │
│  ├─ ir_color_modes.c   伪彩色映射 / LUT                        │
│  ├─ lang.c             i18n                                   │
│  └─ dev_sys.c          电源管理                                │
├──────────────────────────────────────────────────────────────┤
│  移植/驱动层                                                   │
│  ├─ app_main.c         LVGL 移植（flush_cb / read_cb / tick）  │
│  ├─ mlx90640/*         传感器官方驱动                          │
│  └─ bsp/bsp_i2c.c      I2C 总线封装                            │
├──────────────────────────────────────────────────────────────┤
│  系统层                                                        │
│  ESP-IDF 5.3.5 + FreeRTOS + LVGL 8.3.11 + esp_lcd + NVS        │
└──────────────────────────────────────────────────────────────┘
```

### 3.2 任务与并发模型

| 任务 | 入口 | 核心 | 优先级 | 栈 | 干什么 |
|---|---|---|---|---|---|
| `main_task` | `app_main()` | 0 | 1 | 默认 | 初始化所有硬件，建完任务后退出 |
| `LVGL` | `example_lvgl_port_task` | **0** | 2 | 8 KB | 死循环 `lv_timer_handler()`，驱动所有 UI 刷新 |
| `ir_camera_task_main` | 同名函数 | **1** | **5** | 6 KB | 死循环读传感器 → 算法 → 写显示缓冲 → 通知 LVGL |

**双核分工的意图**：UI 渲染和红外算法各占一个核，互不拖慢。算法核优先级更高（5 > 2），保证 16 Hz 采样不掉帧。

其他「定时器」类：

| 定时器 | 类型 | 周期 | 归属 |
|---|---|---|---|
| LVGL tick | `esp_timer` | **2 ms** | `app_main.c` 调 `lv_tick_inc(2)` |
| `s_ui_timer` | `lv_timer` | 500 ms | 曲线页在打开状态下刷新图表 |
| `s_activity_timer` | `lv_timer` | 150 ms | 检测触摸 → 唤醒屏幕；超时 → 关背光 |
| `s_toast_timer` | `lv_timer` | 2000 ms | 主界面提示条消失 |
| `s_fixture_save_timer` | `lv_timer` | 1500 ms | 定点坐标防抖后写 NVS |
| `s_chart_save_hint` | `lv_timer` | 1200 ms | 曲线「保存」按钮图标复位 |

### 3.3 共享资源与同步

这是最容易出 bug 的地方，务必理解：

1. **LVGL 不是线程安全的。** 算法任务要改 UI（换图、改标签、挪气泡），必须先拿锁：
   ```c
   if (example_lvgl_lock(100)) {     // 递归互斥量 lvgl_mux
       lv_img_set_src(img_thermal, &s_thermal_img_dsc);
       ...
       example_lvgl_unlock();
   }
   ```
   UI 线程（LVGL task）自己也会拿同一把锁。**任何在非 LVGL 任务里调用 `lv_*` 而不加锁的代码都是 bug。**

2. **曲线环形缓冲有临界区。** `s_chart_buf` / `s_chart_head` / `s_chart_count` 被 `portMUX_TYPE s_chart_mux` 保护：
   - 写：`ir_camera_chart_push()`（算法任务，500 ms 一次）
   - 读：`ir_camera_chart_get_data()`（UI 线程，500 ms 一次）

3. **显示双缓冲靠指针切换**，见 [6.2](#62-双缓冲)。

4. **`g_ir_settings` 全局设置** 被两个任务同时读写，但没有加锁 —— 属于「宽data race但实际安全」的设计（字段都是 bool/int/float 单字对齐，读写原子；只有「同时改两个字段」的极端时序会短暂不一致）。改动时注意别在里面加多字段强一致逻辑。

### 3.4 内存布局

| 缓冲 | 大小 | 位置 | 谁分配 |
|---|---|---|---|
| LVGL 绘制缓冲 ×2 | 240 × 45 × 2 B = **21,600 B** 各一份 | 内部 RAM（`MALLOC_CAP_DMA`） | `app_main.c` |
| 横屏旋转临时缓冲 | **10,240 B**（`LV_DISP_ROT_MAX_BUF`） | LVGL 堆 | LVGL 内部 |
| 热像显示缓冲 ×2 | 280 × 240 × 2 B = **134,400 B** 各一份 | **PSRAM** | `ir_camera_app_init()` |
| MLX 工作数组 | ≈ 17 KB（834×2 + 768×4×4 + 768 + 参数结构体） | 内部堆 | `ir_camera_task_main()` |
| 照片渲染缓冲 | 134,400 B | PSRAM 优先 | `lvgl_dispaly_ir_mlx90640()` |

- LVGL 自身堆走**系统堆**（`CONFIG_LV_MEM_CUSTOM=y`），不再受 128 KB 内置池限制。
- 若 PSRAM 分配失败，第二块热像缓冲退化为与第一块同一地址（单缓冲模式，`s_display_buf[0] == s_display_buf[1]`）。

---

## 4. 启动流程

`app_main()` 严格按这个顺序执行（顺序有依赖，别乱调）：

```
1.  power_on()                          拉高 IO36 锁电源（最重要，否则松手掉电）
2.  GPIO18 配置为输出，先关背光         避免上电白屏闪烁
3.  spi_bus_initialize(SPI2)            SCLK/MOSI
4.  esp_lcd_new_panel_io_spi()          DC/CS 绑定
5.  esp_lcd_new_panel_st7789()          面板驱动
6.  panel reset / init / invert(true)   ★ invert 必须为 true，否则颜色反
7.  panel_set_gap(0, 20)                ★ 面板有 20px 行偏移（见 14.1）
8.  panel_mirror(false, false)          ★ 面板层不要旋转，交给 LVGL
9.  panel_disp_on_off(true)
10. 开背光 GPIO18 = 1
11. lv_init() + 分配两块 LCD_BUF_LINES(45) 行缓冲
12. lv_disp_drv_register()              ★ sw_rotate=1 + LV_DISP_ROT_270 → 逻辑 280×240
13. app_touch_init()                    建 I2C1、装 CST816S
14. lv_indev_drv_register()             注册触摸；long_press_time = 500 ms
15. esp_timer 每 2 ms → lv_tick_inc(2)
16. 建 lvgl_mux（递归互斥量）
17. 创建 LVGL 任务（core 0, prio 2）
18. lang_init()                         NVS 读语言；首次会 nvs_flash_init
19. photo_init()                        查找 storage 分区
20. boot_show_splash()                  开机图，阻塞 1.5 s
21. if (!lang_configured())             首次开机 → 阻塞式语言选择
        boot_show_language_select()
22. ir_camera_app_init()                ★ 必须先分配 PSRAM 显存再建 UI
23. ir_settings_ui_init()               建全屏设置页（隐藏状态）
24. lvgl_dispaly_ir_mlx90640(disp)      ★ 建 HUD + 相册 + 遥控菜单（需持锁）
25. xTaskCreatePinnedToCore(ir_camera_task_main, core 1, prio 5)
```

> **两条硬性顺序约束**
> 1. `ir_camera_app_init()` 必须在 `lvgl_dispaly_ir_mlx90640()` **之前**：后者要把
>    `s_thermal_img_dsc.data` 指向已分配的显存。
> 2. `lang_init()` 必须在任何 UI 创建之前：所有控件创建时就要按当前语言取字体。

---

## 5. 热成像数据流水线（核心）

全部在 [`../main/ir_mlx90640_app.c`](../main/ir_mlx90640_app.c) 的 `ir_camera_task_main()` 里，`while(1)` 一帧一轮。

### 5.1 总览

```
MLX90640 (I2C, 16 Hz, chess 模式)
   │  MLX90640_GetFrameData()  → 834 个 uint16：0..831 数据、832 控制、833 子场号
   ▼
[1] MLX90640_GetTa()            环境温度，并做 0.12 系数的一阶平滑
   ▼
[2] MLX90640_CalculateTo()      官方温度计算（发射率 + 校准参数）→ 768 个 float 摄氏度
   ▼
[3] MLX90640_BadPixelsCorrection() ×2   坏点 + 离群点修复
   ▼
[4] update_temporal_and_deinterlace()   时域自适应滤波 + 消棋盘格
   ▼
[5] spatial_median_outlier_filter()     3×3 鲁棒中值，带指缝连通保护
   ▼
[6] apply_spatial_filter()      按设置选 双边 / 高斯 / 中值 / 关闭
   ▼
[7] 叠加 temperature_offset_c   出厂校准偏移
   ▼
[8] 统计 min/max + 位置；5%/95% 分位数 + 平滑 → 颜色映射量程
   ▼
[9] temperature_to_norm_u8()    温度 → 0..255（含 gamma 曲线）
   ▼
[10] bilinear_scale_fullscreen_1to1()   32×24 → 280×240，同时查 256 级 LUT 得到 RGB565
   ▼
[11] 画标记 + 写 PSRAM 缓冲 + 换指针 + 持锁通知 LVGL
```

### 5.2 每一步详解

#### [1] 环境温度平滑
```c
float raw_ta = MLX90640_GetTa(ir_frame_data, mlx_params);
smooth_ta += 0.12f * (raw_ta - smooth_ta);
```
裸 `Ta` 抖动会让**全画面温度整体漂移**，所以先做 EMA。用平滑后的 `smooth_ta - TA_SHIFT`
（`TA_SHIFT = 8`，官方经验补偿）参与计算。

#### [2] 官方温度计算
```c
MLX90640_CalculateTo(ir_frame_data, mlx_params, g_ir_settings.emissivity, smooth_ta - TA_SHIFT, raw_temp_matrix);
```
MLX90640 一次只输出**一半像素**（棋盘格的两个子场交替），`CalculateTo` 会基于当前子场增量更新对应 384 个点。
所以 `raw_temp_matrix` 里始终是「一半新 + 一半旧」。

#### [3] 坏点校正
`mlx_params->brokenPixels`（坏点）和 `outlierPixels`（离群点）来自 EEPROM，官方算法用邻居插值修复。

#### [4] 时域滤波 + 消交错（本项目最精妙的一段）
函数 `update_temporal_and_deinterlace()` 分两个阶段：

**阶段 A**：只处理当前子场新采样的 384 点，做**自适应 EMA**：
```
d = |raw - prev|
d ≤ 0.75℃            → alpha = 0.04   （强滤，压静态噪声）
d ≥ 2.20℃            → alpha = 0.85   （弱滤，手移过来立刻响应）
中间                  → alpha 线性插值
filt = prev + alpha * (raw - prev)
```
好处：静止画面极稳，突然出现高温物体又不拖影。

**阶段 B**：处理上一子场的 384 个「旧」像素，判断它们是否因热源移动而滞后：
- 取 4 个**正交邻居**（上下左右），它们都是刚在阶段 A 更新过的最新值
- 排序得到 `n0 ≤ n1 ≤ n2 ≤ n3`，`ortho_span = n3 - n0`
- **平坦区**（`span < 0.90℃`）：用内区间 `[n1, n2]`，越界超过 0.50℃ 就往中位数拉 → 消除奇偶场棋盘噪点
- **结构区**（`span ≥ 0.90℃`，手指边缘/指缝）：用外区间 `[n0, n3]` → 只要有一个方向连着低温指缝，就**绝不把指缝填成高温**（避免「趾蹼粘连」）

#### [5] 3×3 鲁棒中值
`spatial_median_outlier_filter()`：9 邻域排序后，
- 平坦区（`v[n-2] - v[1] < 1.5℃`）→ 与**中位数** `v[4]` 比较，积极滤噪
- 结构区 → 改用区间 `[v[1], v[n-2]]`，保证 1 像素宽的指缝不会被削断

#### [6] 可选空间滤波
`apply_spatial_filter()` 按 `g_ir_settings.filter_mode`：

| 枚举 | 界面上叫 | 实际做的 |
|---|---|---|
| `IR_FILTER_BSPLINE`（默认） | B-Spline | 双边滤波（与原版一致） |
| `IR_FILTER_BILATERAL` | Bilateral | 双边滤波 |
| `IR_FILTER_GAUSSIAN` | Gaussian | 高斯模糊 |
| `IR_FILTER_MEDIAN` | Median | 直接用第 [5] 步结果 |
| `IR_FILTER_OFF` | Off | 跳过（只保留时域滤波） |

#### [7] 校准偏移
`g_ir_settings.temperature_offset_c`（±10℃，步进 0.1）加到每个像素，用于对齐参考温度计。
放在滤波**之后**，不会污染滤波器的历史状态。

#### [8] 量程决策（决定画面对比度）
```c
#define THERMAL_USE_AUTO_RANGE  1     // 1=自动，0=固定 THERMAL_FIXED_MIN_C(24)~MAX_C(35)
```
自动模式的逻辑：
1. 统计有效像素（`1℃ < t < 200℃` 且有限）的最小/最大值
2. `n_valid ≥ 100` 时排序取 **5% / 95% 分位数**（`median_temp` 数组被复用为排序工作区）→ 单个异常点拉不爆画面
3. `target_min = robust_min - 0.4`，`target_max = robust_max + 1.3`（顶部留头空间防止高光过曝）
4. 若 `span < 10℃`，强制撑到 10℃（否则背景噪声被放大成花斑）
5. 用 `alpha = 0.04` 对量程做 EMA 平滑 → 画面不会「呼吸」

#### [9] 归一化 + Gamma
`temperature_to_norm_u8()`：
```c
norm = clamp((t - min) / (max - min), 0, 1);
curve = (gamma == 1.5f) ? norm * sqrtf(norm) : powf(norm, gamma);   // 默认 1.5 走快路径
norm = 0.65f * curve + 0.35f * norm;   // 温和 S 曲线
dst[i] = (uint8_t)(norm * 255.0f + 0.5f);
```
`0.65*gamma + 0.35*linear` 的设计目的：压低背景暗部噪点，同时保住高温区和边缘细节。

#### [10] 双线性插值 + 查表上色
`bilinear_scale_fullscreen_1to1()` 把 32×24 的 `uint8` 归一化矩阵放大到 280×240 的 RGB565：
- 用定点数（`<<16` 步进）算权重，避免每像素浮点除法
- 每像素 4 邻域加权 → 查 256 级 `s_color_lut[]` → 直接得到 RGB565
- **同时处理旋转/镜像**（见 5.4）

#### [11] 标记 + 提交显示
- 最高温画红色菱形（`0xF800`，半径 5），最低温画青色菱形（`0x07FF`）
- 定点标记：未启用定点时画白色小点（半径 3）在屏幕中心，启用后画绿色菱形（半径 5）
- 写入 `s_display_buf[s_back_idx]` → 切指针 → 持锁 `lv_img_set_src()` + `lv_obj_invalidate()`
- 同一把锁里顺带更新 FPS 标签、信息面板、温度气泡位置

### 5.3 关键参数速查

| 宏 | 值 | 在哪 | 含义 |
|---|---|---|---|
| `RefreshRate` | `FPS16HZ` | `ir_mlx90640_app.h` | 传感器刷新率 |
| `THERMAL_USE_AUTO_RANGE` | `1` | `ir_mlx90640_app.c` | 1=自动量程，0=固定 |
| `THERMAL_FIXED_MIN_C/MAX_C` | 24.0 / 35.0 | 同上 | 固定量程范围 |
| `TEMP_FILTER_ALPHA_MIN/MAX` | 0.04 / 0.85 | 同上 | 时域滤波 α 上下限 |
| `TEMP_FILTER_NOISE_FLOOR_C` | 0.75 | 同上 | 视为噪声的温差 |
| `TEMP_FILTER_MOTION_SAT_C` | 2.20 | 同上 | 视为运动的温差 |
| `THERMAL_OUTLIER_THRESH_C` | 0.58 | 同上 | 孤立噪点阈值 |
| `AUTO_RANGE_MIN_SPAN_C` | 10.0 | 同上 | 自动量程最小跨度 |
| `AUTO_RANGE_LOW_PADDING_C` | 0.4 | 同上 | 底部留白 |
| `AUTO_RANGE_HIGHLIGHT_HEADROOM_C` | 1.3 | 同上 | 顶部留白 |
| `AUTO_RANGE_ALPHA` | 0.04 | 同上 | 量程平滑系数 |
| `THERMAL_PERCENTILE_LOW/HIGH` | 5 / 95 | 同上 | 分位数 |
| `TA_SHIFT` | 8 | `ir_mlx90640_app.h` | 环境温度补偿 |
| `IR_CHART_HISTORY_LEN` | 60 | 同上 | 曲线点数 |
| `IR_CHART_SAMPLE_MS` | 500 | 同上 | 曲线采样周期 → 30 s 窗口 |

### 5.4 显示坐标变换（旋转 + 镜像）

这是第二个易错点。**屏幕坐标**是 LVGL 逻辑坐标（280×240 横屏），**原始坐标**是传感器 32×24。

三个函数互相配合，都受 `g_ir_settings.rotate` / `mirror_vertical` / `mirror_horizontal` 影响：

| 函数 | 方向 | 用途 |
|---|---|---|
| `screen_xy_to_raw_idx()` → `screen_xy_to_source_fp()` | 屏幕 → 原始 | 触摸定点、中心点取值 |
| `raw_rc_to_screen_xy()` | 原始 → 屏幕 | 最高/最低温标记与气泡定位 |
| `bilinear_scale_fullscreen_1to1()` | 原始矩阵 → 屏幕图像 | 出图 |

关键点：**镜像和旋转是耦合的**。代码里做了补偿：
```c
const bool flip_x = (rot == IR_ROT_90 || rot == IR_ROT_180) ? !mirror_x : mirror_x;
const bool flip_y = (rot == IR_ROT_180 || rot == IR_ROT_270) ? !mirror_vertical : mirror_vertical;
```
所以「把 rotate 改 180°」时，镜像开关的视觉效果会反过来。改这块一定要三者一起想，否则图上标记和实际温度点会对不上。

`ir_camera_update_transform()` 负责在旋转/镜像改变时重算缩略步进 `s_source_col_step_fp` / `s_source_row_step_fp`。

---

## 6. 显示子系统

### 6.1 屏幕 180° 旋转方案

物理面板是**竖屏 240×280**，但 UI 要按**横屏 280×240** 布局，还要整体再翻 180°。实现方式：

```c
/* app_main.c */
#define UI_DISP_ROTATION   LV_DISP_ROT_270
disp_drv.rotated     = UI_DISP_ROTATION;
disp_drv.sw_rotate   = 1;          // 让 LVGL 做软件旋转
```

工作方式：
1. 设置 `hor_res/ver_res = 240/280`（物理尺寸）
2. LVGL 看到 `sw_rotate = 1` 后，把**逻辑分辨率自动换成 280×240**，所有 UI 坐标都按横屏写
3. 每次 flush，LVGL 自己在临时缓冲里把像素旋转成物理坐标，再回调 `flush_cb`
4. **触摸坐标也由 LVGL 自动反向旋转**，驱动层不用管

**三条铁律**：
- 不要再用 `esp_lcd_panel_swap_xy()` / `esp_lcd_panel_mirror()` 转面板 → 会双重旋转
- 触摸配置里 `swap_xy/mirror_x/mirror_y` 必须全为 0
- 触摸 Y 坐标要减 20（`esp_lcd_panel_set_gap(0, 20)` 造成），见 `example_lvgl_touch_cb()`

改方向只改 `UI_DISP_ROTATION` 一个宏即可（`LV_DISP_ROT_NONE / 90 / 180 / 270`）。

### 6.2 双缓冲

```c
static uint16_t *s_display_buf[2];   // 各 280×240 RGB565 = 134,400 B，在 PSRAM
static uint8_t   s_back_idx = 0;     // 当前可写的那块
```
流程：
1. 算法任务往 `s_display_buf[s_back_idx]` 里画完整一帧
2. 持锁，把 `s_thermal_img_dsc.data` 指到这块，`lv_img_set_src()` + `lv_obj_invalidate()`
3. 立刻 `s_back_idx = 1 - s_back_idx`，下一帧写另一块

这样 LVGL 正在读 A 块显示时，算法任务在写 B 块，不会撕裂。
若 PSRAM 不足导致 `s_display_buf[1] == s_display_buf[0]`，指针不切换（单缓冲，可能轻微撕裂但不会崩）。

### 6.3 LVGL 移植要点

| 回调 | 位置 | 作用 |
|---|---|---|
| `example_lvgl_flush_cb` | `app_main.c` | `esp_lcd_panel_draw_bitmap()` 把 LVGL 缓冲刷到屏 |
| `example_notify_lvgl_flush_ready` | 同上 | DMA 传输完成回调 → `lv_disp_flush_ready()` |
| `example_lvgl_touch_cb` | 同上 | 读 CST816S → 填 `data->point` / `state` |
| `example_increase_lvgl_tick` | 同上 | `esp_timer` 每 2 ms → `lv_tick_inc(2)` |
| `example_lvgl_port_task` | 同上 | `lv_timer_handler()` 死循环 |
| `example_lvgl_lock/unlock` | 同上 | 递归互斥量，跨任务访问 LVGL 的唯一入口 |

---

## 7. UI 子系统

### 7.1 两套 UI 的关系（新人最容易迷路的地方）

代码里有**两套完全独立的菜单**：

| | A. 全屏设置页 | B. 遥控预览菜单 |
|---|---|---|
| 文件 | `ir_settings_ui.c` | `lvgl_demo_ui.c` |
| 根容器 | `s_root`（全屏 280×240 白底 + 蓝色顶栏） | `s_menu_backdrop`（半透明遮罩 + 居中面板） |
| 入口 | 长按屏幕 → `ir_settings_ui_open()` | ⚠️ 只有 `thermal_ui_show_gallery()` 会用到它 |
| 页面 | 15 个 `PAGE_*` | 一级菜单 / 调色板 / 语言 / 相册 |
| 状态 | **当前主用** | **历史遗留，仅相册面板仍被复用** |

具体来说，`create_settings_menu()` 建出来的 `s_main_menu_panel`、`s_palette_panel`、
`s_language_panel` 现在**没有任何路径能显示**（`s_menu_backdrop` 创建后立刻被隐藏，
只有相册流程会把它重新显示并切到 `s_gallery_panel`）。
调试时如果发现「点了没反应」，先确认你改的是不是 A 那套。

### 7.2 主界面 HUD（`lvgl_dispaly_ir_mlx90640`）

层级（都在 `ui_root` 这个 280×240 容器里，`ui_root` 不可点击，事件穿透到 `scr`）：

```
ui_root
├── img_thermal          全屏热像图（280×240，zoom=256 即 1:1）
├── top_capsule          顶部标题胶囊：MODE: 铁红
├── label_fps_float      左上 FPS 药丸
├── tag_emissivity       右上发射率药丸（e:0.95）
├── tag_max / tag_min    最高/最低温气泡（跟随菱形标记定位）
├── tag_fix              定点温度气泡（默认隐藏）
├── bottom_capsule       底部信息卡：MAX/MIN/中心或定点/温差
├── touch_cursor         触摸准星（30px 圆环，按下显示）
├── touch_label          X/Y 坐标显示
├── s_toast              居中提示条（2 s 消失）
├── s_viewer_overlay     照片查看器全屏覆盖层（默认隐藏）
└── [settings 的两个根]   s_menu_backdrop / s_root
```

### 7.3 手势（全在 `screen_touch_event_cb`）

| 手势 | 判定 | 结果 | 关键常量 |
|---|---|---|---|
| **长按** | 按住 ≥ 500 ms 且位移 ≤ 24 px | 打开全屏设置页 | `MENU_HOLD_TIME_MS=500`、`MENU_HOLD_TOLERANCE_PX=24` |
| **左右滑** | 位移 ≥ 24 px 且横向占优 | 切换配色 | `COLOR_SWIPE_MIN_DX=24`、`COLOR_SWIPE_MOVE_PX=5` |
| **单击** | 位移 ≤ 12 px | 若启用定点，设定定点坐标（1.5 s 后落盘） | `TAP_MOVE_PX=12` |
| **双击** | 700 ms 内两次单击 | 拍照存相册 | — |
| 菜单内左右滑 | ≥ 60 px | 退出菜单 | `MENU_SWIPE_EXIT_DX=60` |

**防误触设计**：长按松手的那一刻很容易触发一次 `CLICKED`，所以要靠 `menu_click_guard()`：
菜单刚打开的 400 ms（`lvgl_demo_ui.c`）/ 500 ms（`ir_settings_ui.c`）内，所有菜单项点击都被忽略。

### 7.4 全屏设置页

**结构**：
```
s_root (280×240)
├── s_header  (280×40)   蓝色顶栏，整条可点＝返回上一级/关闭
└── s_body    (280×200)  每页 `lv_obj_clean()` 后重建
```

**页面枚举**（`settings_page_t`）：

```c
PAGE_MAIN → PAGE_COLOR → PAGE_LABELS → PAGE_CHART → PAGE_ROTATE → PAGE_MIRROR
→ PAGE_SCALE → PAGE_SCREEN → PAGE_FILTER → PAGE_EMISSIVITY → PAGE_GAMMA
→ PAGE_CALIBRATION → PAGE_LANGUAGE → PAGE_FACTORY_RESET → PAGE_ABOUT
```

**渲染机制**（重要）：换页永远是「清空 + 重建」，**不是**隐藏/显示：

```c
static void render_page(settings_page_t page) {
    s_current_page = page;
    ...指针置空、删除残留定时器...
    lv_obj_clean(s_body);           // ← 销毁上一页所有对象
    switch (page) { case PAGE_COLOR: render_color_page(); break; ... }
}

static void schedule_page(settings_page_t page) {
    lv_async_call(render_page_async_cb, (void*)page);   // ← 异步，避免在点击回调里删自己
}
```
**必须用 `schedule_page()` 而不是直接 `render_page()`**：在某个按钮的点击回调里把包含该按钮的
容器删掉，LVGL 会访问已释放内存 → 崩溃。异步调用会推迟到下一轮事件循环。

**主菜单条目**（`render_main_page`）：

| 分组 | 条目 | 目标 |
|---|---|---|
| 基本设置 | 色彩样式 / 温度标签 / 曲线 / 相册 / 旋转 / 镜像 / 温度单位 / 屏幕 | 各自 `PAGE_*`；相册走 `gallery_event_cb` |
| 高级设置 | 滤波 / 发射率 / 伽马 / 校准 | `PAGE_*` |
| 其他 | 语言 / 恢复出厂（红）/ 关于 / 关机（红） | `PAGE_*`；关机走 `settings_power_off_event_cb` |

**生命周期接口**：
| 函数 | 作用 |
|---|---|
| `ir_settings_ui_init()` | 载入设置、初始化背光 PWM、找触摸设备、注册两个 lv_timer |
| `ir_settings_ui_create(parent)` | 建控件树（初始隐藏） |
| `ir_settings_ui_open() / close()` | 显示/隐藏；`close()` 会先 `ir_camera_settings_save()` |
| `ir_settings_ui_is_open()` | 主界面手势要判断它，避免菜单打开时还响应滑动/双击 |
| `ir_settings_ui_refresh_language()` | 切语言后重建当前页 |
| `ir_settings_ui_note_activity()` | 触摸时重置休眠计时 |

**曲线页**：`lv_chart`（232×100）+ 手工加的 3 个 Y 轴标签 + 3 个 X 轴标签（`-30s / -15s / 0s`）。
`chart_refresh()` 每 500 ms 被 `ui_timer_cb` 调一次，从环形缓冲取 60 点换算成当前温度单位。

### 7.5 相册与照片查看器

- 相册面板挂在 `s_gallery_panel`（整屏白底 + 蓝色顶栏 + 容量条 + 列表）
- 容量条：`lv_bar` 0~1000 表示占用千分比；文字 `48.0KB / 2.0MB` 由 `format_size()` 生成
- 列表复用了 `create_list_entry()`，行宽 `UI_SCREEN_W`
- 点某一行 → `open_viewer(idx)`：读归一化矩阵 → `ir_camera_render_norm()` 重新上色 → 全屏显示
- 查看器支持左右滑切换（`viewer_touch_cb`，阈值 40 px）、删除（`viewer_delete_cb`）

### 7.6 Toast

`show_toast(msg)`：居中白底蓝边气泡，`lv_obj_move_foreground` 保证在最上层，2 s 后自动隐藏。
用于「已保存」「存储已满」。

---

## 8. 配置持久化（NVS）

### 8.1 三个命名空间

| 命名空间 | Key | 内容 | 谁管 |
|---|---|---|---|
| `ir_settings` | `app` | 整个 `ir_app_settings_t` 结构体（blob） | `ir_camera_settings_load/save()` |
| `thermal` | `lang` | 当前语言（int32） | `lang_init/set()` |
| `ir_chart` | `count` / `samples` | 曲线快照（int32 + float 数组） | 曲线页「保存」按钮 |

> ⚠️ `nvs_flash_init()` 是在 `lang_init()` 里调的，所以**任何 NVS 操作都必须发生在 `lang_init()` 之后**。

### 8.2 版本控制机制（改默认值必读）

结构体外面套了一层头：

```c
typedef struct {
    uint32_t magic;      // 0x49525354 ("IRST")
    uint32_t version;    // 当前 IR_SETTINGS_VERSION = 3
    ir_app_settings_t settings;
} ir_settings_nvs_blob_t;
```

载入逻辑（`ir_camera_settings_load`）：

```c
g_ir_settings = s_default_settings;          // ① 先套默认值
if (读到 blob && magic 对 && version 对) {
    g_ir_settings = blob.settings;           // ② 用存的值覆盖
} else {
    need_save_defaults = true;               // ③ 版本不符/首次/损坏 → 保持默认并回写
}
```

**推论（非常重要）**：
- **空白 flash 首次烧录** → 走 ③ → 得到 `s_default_settings` 里的默认值
- **刷固件不会清 NVS** → 只要 `IR_SETTINGS_VERSION` 不变，旧 blob 一直有效，**改了默认值也看不出来**
- 想让新默认值对老设备生效，必须二选一：
  1. `IR_SETTINGS_VERSION` 加 1（代价：所有设置回默认）
  2. `idf.py erase-flash`（全清，连照片和语言一起清）

### 8.3 字段表

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `color_mode` | enum | `IR_COLOR_IRONBOW` | 9 种伪彩 |
| `color_reverse` | bool | false | LUT 反色 |
| `label_coldest` | bool | true | 显示最低温标记 |
| `label_hottest` | bool | true | 显示最高温标记 |
| `label_fixture` | bool | false | 定点模式（false = 用屏幕中心） |
| `fixture_px_x/y` | int | 140 / 120 | 定点屏幕坐标 |
| `rotate` | enum | `IR_ROT_0` | 图像旋转 0/90/180/270 |
| `mirror_vertical` | bool | **true** | 上下镜像 |
| `mirror_horizontal` | bool | **true** | 左右镜像（⚠️ 载入/保存时会被 `IR_FORCE_HORIZONTAL_MIRROR` 强制覆写） |
| `temp_scale` | enum | Celsius | C / F / K / R |
| `brightness` | int | 50 | 背光 5~100 |
| `auto_sleep_min` | int | 30 | 自动休眠分钟数，0 = 不休眠 |
| `filter_mode` | enum | `IR_FILTER_BSPLINE` | 空间滤波 |
| `emissivity` | float | 0.95 | 发射率 0.10~1.00 |
| `gamma` | float | 1.50 | 0.50~3.00 |
| `temperature_offset_c` | float | 0.0 | 校准偏移 -10~+10 |
| `chart_source` | enum | `IR_CENTER` | 曲线采样源 |

> 载入后会调 `settings_clamp_to_valid_ranges()` 把越界值夹回合法范围，防止旧 blob/脏数据导致异常。

---

## 9. 多语言与字体

### 9.1 字符串表

- 定义在 `_gen_fonts.py` 的 `STRINGS` 字典里（这是**唯一真源**）
- 脚本生成 `lang.h`（枚举 `lang_str_id_t`）和 `lang_data.c`（`s_str_table[STR_COUNT][LANG_COUNT]`）
- 运行时用 `lang_str(STR_XXX)` 取当前语言文案；`lang_name(l)` 取语言自称
- 字体用 `lang_font()`（当前语言）或 `lang_font_for(l)`（指定语言）
- 语言存在 NVS `thermal/lang`，空白 flash 无值 → `lang_configured()` 为 false → 首次开机弹语言选择

### 9.2 字体子集生成（改文案必读）

`_gen_fonts.py` 为 ZH/JA/KO/RU 各生成一套 16 px、4 bpp 的字体 **子集**：

```python
# 换字体只需改这里（家族 + 字号），再重新跑脚本
UI_FONT_FAMILY = "NotoSansSC"   # "NotoSansSC"(思源黑体) / "MicrosoftYaHei"(微软雅黑)
UI_FONT_SIZE   = 16

FONT_SPECS = {
    "ZH": ("C:/Windows/Fonts/Noto Sans SC Medium (TrueType).otf", 0, 16),
    "JA": ("C:/Windows/Fonts/msgothic.ttc",                       0, 16),
    "KO": ("C:/Windows/Fonts/malgun.ttf",                         0, 16),
    "RU": ("C:/Windows/Fonts/segoeui.ttf",                        0, 16),
}
```

> 历史：ZH 原本用 `simsun.ttc`（宋体，衬线印刷体），16 px 小屏上笔画细、发虚，
> 与主流热像仪 UI 的现代无衬线风格不符，已换成**思源黑体 Medium**。
> RU 由 Arial 换成 Segoe UI，观感更统一。
> Noto Sans SC 为 OFL 授权，可商用。

字体是**编译期烧进固件的位图**，运行时不需要系统装字体。

**行高归一化**：CJK 字体的 hhea 行高在 16 px 下会到 24 px（SimSun 只有 17 px），
直接使用会让两行 HUD 文本溢出 44 px 的底部卡片。脚本会按字形实际墨迹范围
（`box_h + ofs_y` / `ofs_y`）收紧 `line_height` / `base_line`，保证不裁切墨迹：
ZH 19 px、JA 16 px、KO 17 px、RU 17 px。

包含的字符 = ASCII 0x20~0x7E + `STRINGS`/`LANG_NAMES` 里出现过的所有非 ASCII 字符。

**结论**：
- ❌ **不要在代码里写死中文/日文/韩文字面量**（比如 `"已用"`），这些字符不在子集里 → 显示成空白或方块
- ✅ 需要新文案时：改 `_gen_fonts.py` 的 `STRINGS` → 重新跑脚本 → 重新编译
- ✅ 只需要数字/英文的地方直接用 `lv_font_montserrat_14`，不占额外空间
- 脚本对 4 bpp 做了「`box_w` 补齐偶数」处理，避免行跨度不对齐导致的**文字出现横线/乱码**

运行脚本（需要 Python + Pillow）：
```bash
cd project/spi_lcd
python _gen_fonts.py
# 会重写 main/lang.h、main/lang_data.c、main/app_font_{zh,ja,ko,ru}.c
```

---

## 10. 照片存储

[`../main/photo.c`](../main/photo.c) 直接在 `storage` 分区上做**裸扇区读写**（虽然分区类型标的是 spiffs，但并没有挂载文件系统）。

**单张照片布局**（固定 4 KB 一个槽位）：

| 偏移 | 大小 | 内容 |
|---|---|---|
| +0 | 4 B | magic `"PHT1"` |
| +4 | 4 B | `valid`（1 = 有效，0xFF = 已删除） |
| +8 | 4 B | 时间戳（秒） |
| +12 | 4 B | 色彩模式 |
| +16 | 8 B | 拍照时的 min_t / max_t |
| +24 | **768 B** | 32×24 归一化温度矩阵 |
| +792 | ~3.2 KB | 填充（未用） |

设计要点：
- **不存图像，存温度**。280×240 RGB565 要 134 KB，存归一化矩阵只要 768 B（**约 170 倍压缩**），查看时用 `ir_camera_render_norm()` 按当前配色重新渲染
- 分区 2 MB ÷ 4 KB = **最多 512 张**
- 删除 = 只擦头 4 KB 扇区（把 `valid` 变成 0xFF），不整片擦除
- `photo_count()` 每次都扫一遍所有槽位（512 次 4 B 读取），调用频率低，可接受

容量查询接口（相册页用）：
```c
uint32_t photo_total_bytes(void);   // = 分区大小（2 MB）
uint32_t photo_used_bytes(void);    // = 照片数 × 4096
```

---

## 11. 电源与背光

### 11.1 电源自锁（`dev_sys.c`）

```
按下开机键 → 硬件给电 → app_main 第一件事 power_on() → IO36 输出高 → 自锁保持
                                                              ↓
松开按键后靠 IO36 继续供电，不会掉电
```
关机：`sys_power_off()` → `gpio_set_level(IO36, 0)` → 断电。
（IO37 是按键状态输入，目前只配置未使用，可用于按键关机。）

`sys_start_auto_poweroff(seconds)` 用一次性 `esp_timer` 实现定时关机。

### 11.2 背光 PWM

- GPIO18，`LEDC_TIMER_0` / `LEDC_CHANNEL_0`，**5 kHz**，**10 bit**
- `apply_backlight()`：`duty = 1023 * brightness / 100`，亮度夹在 5~100
- 休眠时 duty 直接置 0（不关机，只关背光），触摸立刻恢复

### 11.3 自动休眠

`s_activity_timer`（150 ms）：
1. 若触摸处于按下状态 → `screen_wake()` 重置计时
2. 若 `auto_sleep_min > 0` 且静默超过 `auto_sleep_min` 分钟 → 关背光

---

## 12. 构建、烧录与调试

### 12.1 环境

- ESP-IDF **v5.3.5**（本文档对应 `C:\esp\v5.3.5\esp-idf`）
- 目标芯片 `esp32s3`
- 串口示例 `COM32`（USB-Serial/JTAG）

### 12.2 命令行

```bash
cd project/spi_lcd

# 首次或换芯片时设置目标
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录 + 串口监视（Ctrl+] 退出监视）
idf.py -p COM32 flash monitor

# 只烧 app（不动 bootloader/分区表，最快）
idf.py -p COM32 app-flash

# 全片擦除（连 NVS 一起清，用来验证"全新机器"行为）
idf.py -p COM32 erase-flash
```

VS Code 用户：ESP-IDF 扩展的 `Build` / `Flash` / `Monitor` 按钮等价。

### 12.3 常看的启动日志

```
I (xxx) dev_sys:  Power_successfully_latched_via_IO36!     ← 电源自锁成功
I (xxx) thermal imaging_eg: Register display driver to LVGL
I (xxx) thermal imaging_eg: Initialize CST816 touch driver  ← 触摸起来
I (xxx) CST816S:  IC id: 181                               ← 触摸芯片识别
I (xxx) lang:     loaded language: 中文 / no saved language ← NVS 语言
I (xxx) photo:    storage @0x110000 size=0x200000, max=512  ← 分区找到
I (xxx) mlx_app:  Display transform: rot=0 mirror_x=1 mirror_y=1
I (xxx) mlx_app:  IR camera task started on Core 1
I (xxx) mlx_app:  Warming up MLX90640 sensor...
```

### 12.4 日志 TAG 速查

| TAG | 来源 |
|---|---|
| `thermal imaging_eg` | `app_main.c` |
| `dev_sys` | `dev_sys.c` |
| `mlx_app` | `ir_mlx90640_app.c` |
| `ir_settings` | `ir_settings_ui.c` |
| `thermal_ui` | `lvgl_demo_ui.c` |
| `photo` | `photo.c` |
| `lang` | `lang.c` |
| `bsp_i2c` | `bsp/bsp_i2c.c` |

过滤示例：`idf.py monitor` 里按 `Ctrl+T` `Ctrl+F` 输入 `mlx_app`。

> 注意：`example_lvgl_touch_cb()` 里有一句 **每帧都打印的触摸日志**，
> 调试完建议注释掉，否则串口刷屏会拖慢手感。

---

## 13. 新人常见任务

### 13.1 改默认设置值（如默认配色、默认量程）

1. 改 [`../main/ir_mlx90640_app.c`](../main/ir_mlx90640_app.c) 的 `s_default_settings`
2. `IR_SETTINGS_VERSION` **加 1**
3. 重编译烧录（或者在设备上点「恢复出厂」）

> 只做第 1 步是没用的 —— 见 [8.2](#82-版本控制机制改默认值必读)。

### 13.2 加一个设置页

1. `settings_page_t` 加枚举（放在 `PAGE_ABOUT` 之前）
2. 写 `render_xxx_page()`，第一行调 `create_page_title(lang_str(...))`，之后往 `s_body` 加控件
3. `render_page()` 的 `switch` 加一个 `case`
4. `render_main_page()` 里用 `add_menu_row(list, 文案, PAGE_XXX)` 挂到菜单
5. 需要新文案 → 走 [9.2](#92-字体子集生成改文案必读)

### 13.3 加一种伪彩色

1. `ir_color_modes.h` 的 `ir_color_mode_t` 加枚举（`IR_COLOR_MODE_MAX` 之前）
2. `ir_color_modes.c` 写 `ir_temp_to_xxx_rgb565()`，并在 `ir_temp_to_rgb565()` 的 switch 里加分支
3. `ir_color_get_mode_name()` 加名字
4. `ir_color_generate_lut()` 无需改（它调 `ir_temp_to_rgb565`）
5. `lvgl_demo_ui.c` 的 `s_mode_dot_hex[]` 加一个代表色（预览圆点用）

### 13.4 改屏幕方向 / 翻转

- 整体旋转：改 `app_main.c` 的 `UI_DISP_ROTATION`
- 只翻热像图（UI 不动）：改 `g_ir_settings.rotate` / `mirror_*`，或设置页「旋转 / 镜像」
- ⚠️ 面板层的 `mirror/swap_xy` 不要碰

### 13.5 改引脚 / 换屏

改 `app_main.c` 顶部 `EXAMPLE_PIN_NUM_*`；
若换分辨率，还要同步改 `EXAMPLE_LCD_H_RES/V_RES`、`UI_SCREEN_W/H`、
`MLX_INTERP_W/H`（在 `ir_mlx90640_app.h`）以及各 UI 页面里的坐标常量。

### 13.6 调整热像观感

| 想要的效果 | 调什么 |
|---|---|
| 画面更稳（但响应变慢） | `TEMP_FILTER_ALPHA_MIN` 调小 |
| 画面更灵敏（但噪声大） | `TEMP_FILTER_ALPHA_MIN` 调大 |
| 对比度更强 | `AUTO_RANGE_MIN_SPAN_C` 调小，或调 `gamma` |
| 背景更暗、热源更突出 | 增大 `AUTO_RANGE_LOW_PADDING_C` |
| 高温不过曝 | 增大 `AUTO_RANGE_HIGHLIGHT_HEADROOM_C` |
| 固定量程 | `THERMAL_USE_AUTO_RANGE` 改 0 + 调 `THERMAL_FIXED_MIN_C/MAX_C` |

### 13.7 照片能存多少

改 `partitions.csv` 里 `storage` 的 Size（注意别和 app 分区重叠），
每 4 KB 一张。改完要 `erase-flash` 让新分区表生效。

---

## 14. 已知坑（踩过的雷）

### 14.1 屏幕 20 px 偏移
面板 `set_gap(0, 20)` 后，触摸读到的 Y 要 **减 20** 才能和 LVGL 坐标对齐。
若换屏或改旋转，这个偏移值要重新标定。

### 14.2 `lv_label_set_text_fmt` 里的 `%f` 会崩
LVGL 自带的 printf 若没开 `CONFIG_LV_SPRINTF_USE_FLOAT`，遇到 `%f` 会走 `default`
分支只输出字符 `f` 且 **不推进 va_list**。于是后面若有 `%s`，就会把 double 的位模式
当指针解引用 → `LoadProhibited` 重启，崩溃点还落在 `lv_obj_event_base()` 里，极具迷惑性。

- 现在 `sdkconfig.defaults` 已开 `CONFIG_LV_SPRINTF_USE_FLOAT=y`
- 但**推荐习惯**：浮点一律用标准库：
  ```c
  char buf[48];
  snprintf(buf, sizeof(buf), "%s %.1f %s", label, (double)v, unit);
  lv_label_set_text(obj, buf);
  ```
  （LVGL 的 printf 是精简版，`%f` 精度处理也和标准库不同）

### 14.3 LVGL 内存池只有 128 KB 上限
`CONFIG_LV_MEM_SIZE_KILOBYTES` 的 Kconfig range 最大就是 128，调不上去。
设置页对象一多就 `LV_ASSERT_MALLOC` 直接 `abort()` 重启。
现已改用系统堆：
```ini
CONFIG_LV_MEM_CUSTOM=y
CONFIG_LV_MEM_CUSTOM_INCLUDE="stdlib.h"
```
**不要把这两行删掉。**

### 14.4 `sw_rotate` 的额外内存
横屏软件旋转每次 flush 需要 `240 × 21 × 2 = 10,080 B` 临时缓冲（`LV_DISP_ROT_MAX_BUF=10240`），
且 LVGL 只标记空闲不归还，属于**永久驻留**。算内存时要留出这 10 KB。

### 14.5 主题 GROW 导致按下高亮「溢出」
LVGL 默认主题开了 `CONFIG_LV_THEME_DEFAULT_GROW`，任何 `lv_btn` 按下时会
**向四周各扩 3 px**（`transform_width/height`），再叠加 80 ms 过渡
（`CONFIG_LV_THEME_DEFAULT_TRANSITION_TIME`）。
表现：点菜单行时，蓝/红色块会盖到相邻行上，闪一下就恢复。

修法（二选一或同时）：
```ini
# sdkconfig.defaults
# CONFIG_LV_THEME_DEFAULT_GROW is not set
CONFIG_LV_THEME_DEFAULT_TRANSITION_TIME=0
```
```c
/* 代码兜底：给按钮/列表行钉死按下态几何 */
lv_obj_set_style_transform_width(obj, 0, LV_STATE_PRESSED);
lv_obj_set_style_transform_height(obj, 0, LV_STATE_PRESSED);
lv_obj_set_style_shadow_width(obj, 0, LV_STATE_PRESSED);
lv_obj_set_style_outline_width(obj, 0, LV_STATE_PRESSED);
```

### 14.6 `lv_chart` 必须「先设点数，再加系列」
```c
lv_chart_set_point_count(chart, IR_CHART_HISTORY_LEN);   // ← 必须先
lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);  // ← add_series 按 point_cnt 分配 y_points
```
顺序反了就是堆越界写。另：写 `y_points[]` 时下标必须 `< point_count`。

### 14.7 在点击回调里删自己 → 必崩
必须用 `lv_async_call()` 推迟到下一轮（见 `schedule_page()`）。

### 14.8 字体子集缺字
见 [9.2](#92-字体子集生成改文案必读)。症状：中文显示空白、方框，或字上带横线。

### 14.9 刷固件 ≠ 重置设置
`flash` 只写 app 分区，NVS 里的旧设置、语言、照片都还在。
要复现「全新机器」必须 `erase-flash`。

### 14.10 `IR_FORCE_HORIZONTAL_MIRROR` 会覆盖你的镜像设置
`ir_mlx90640_app.c` 顶部有这个宏（当前 = 1）。它在
`ir_camera_settings_load()` 和 `ir_camera_settings_save()` 里**强制**把
`g_ir_settings.mirror_horizontal` 写回宏值，且三个渲染函数直接用宏而不是用设置值。
所以「在界面里关掉水平镜像，重启又变开着」不是 bug，是这个宏在起作用。
要让它可配置：删掉这个宏和两处强制赋值，把渲染处的 `ir_horizontal_mirror_enabled()`
改成 `g_ir_settings.mirror_horizontal`，并把 `s_default_settings` 里的默认值改成 `false`。

### 14.11 编译优化等级是 Debug
`CONFIG_COMPILER_OPTIMIZATION_DEBUG=y`。图像处理是热路径，若嫌帧率不够，
可切 `-Os` 或 `-O2`（menuconfig → Compiler options → Optimization Level）。
注意 `IRAM` 已用到 **99.99%**，切优化等级前先看 size 报告。

---

## 15. 术语表

| 术语 | 含义 |
|---|---|
| **subpage / 子场** | MLX90640 一次只输出棋盘格的一半像素，两个子场交替合成整帧 |
| **chess 模式** | 上述棋盘格采样模式（`MLX90640_SetChessMode`） |
| **Ta** | 传感器环境温度（Ambient temperature） |
| **发射率 / emissivity** | 物体辐射效率，非金属一般 0.9~0.95 |
| **NUC** | 非均匀性校正（本项目只做了坏点校正，未做完整 NUC） |
| **LUT** | 查找表，这里指 256 级「归一化值 → RGB565」映射表 |
| **HUD** | 主界面上的叠加信息层（标题、FPS、温度气泡等） |
| **sw_rotate** | LVGL 软件旋转（像素级），区别于面板硬件旋转 |
| **归一化矩阵** | 温度 → 0~255 的 uint8 矩阵，照片存储与插值都用它 |
| **定点 / fixture** | 用户指定的测温点，默认用屏幕中心 |

---

## 附：一分钟速查

```
想让它跑起来        → idf.py build && idf.py -p COM32 flash monitor
热像不对劲          → ir_mlx90640_app.c 的 MACRO 区（5.3 表）
界面改动            → 全屏设置页 = ir_settings_ui.c；主界面 = lvgl_demo_ui.c
加文案/加语言        → _gen_fonts.py 的 STRINGS → 重跑脚本 → 重编译
改默认设置           → s_default_settings + IR_SETTINGS_VERSION 加 1
崩溃在 lv_*         → 查 %f、查 LVGL 锁、查是否有对象已删
设置改了没用         → 查 NVS 版本号、查 IR_FORCE_* 宏
```
