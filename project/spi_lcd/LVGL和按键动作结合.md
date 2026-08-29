LVGL 和实体按键结合，本质就是：

**实体按键 → 输入设备(indev) → LVGL事件系统 → 控件回调**

LVGL 不关心你的按键是 GPIO、矩阵键盘、触摸芯片，它只需要你提供：

> 当前有没有按下？按的是哪个键？

---

## 1. LVGL 输入设备结构

LVGL里面有：

```
          实体按键
              |
              |
          GPIO读取
              |
              |
        lv_indev_drv
              |
              |
          LVGL核心
              |
      ----------------
      |              |
   Button事件     Encoder事件
```

---

比如：

按键：

```
GPIO0  ---- KEY1
GPIO1  ---- KEY2
GPIO2  ---- KEY3
```

你扫描：

```c
KEY1按下
```

告诉 LVGL：

```c
LV_KEY_ENTER
```

LVGL就认为：

```
用户按了确认键
```

---

# 2. 注册按键输入设备

例如：

```c
static lv_indev_t *keypad_indev;


lv_indev_drv_t indev_drv;


lv_indev_drv_init(&indev_drv);


indev_drv.type = LV_INDEV_TYPE_KEYPAD;


indev_drv.read_cb = keypad_read;


keypad_indev =
    lv_indev_drv_register(&indev_drv);
```

---

# 3. 写 GPIO按键读取函数

LVGL会周期调用：

```c
keypad_read()
```

例如：

```c
void keypad_read(lv_indev_drv_t *drv,
                 lv_indev_data_t *data)
{

    if(gpio_get_level(KEY_OK)==0)
    {

        data->state =
            LV_INDEV_STATE_PR;


        data->key =
            LV_KEY_ENTER;

    }
    else
    {

        data->state =
            LV_INDEV_STATE_REL;

    }

}
```

意思：

按下：

```
KEY_OK

↓

LV_KEY_ENTER
```

---

# 4. 给页面绑定按键焦点

LVGL按键控制不是直接控制所有控件。

需要：

```c
lv_group_t
```

也就是焦点组。

例如：

创建组：

```c
lv_group_t *group;


group =
lv_group_create();
```

添加控件：

比如：

```c
btn1 =
lv_btn_create(scr);


btn2 =
lv_btn_create(scr);



lv_group_add_obj(group, btn1);
lv_group_add_obj(group, btn2);
```

然后：

```c
lv_indev_set_group(
    keypad_indev,
    group
);
```

---

现在：

按：

```
UP/DOWN
```

可以切换按钮焦点。

按：

```
ENTER
```

触发点击。

---

# 5. 实际项目常用结构

比如你的：

ESP32-S3 + ST7789 + 热成像

可以这样：

```
             GPIO KEY

        UP DOWN OK BACK

              |
              |
        FreeRTOS task

              |
              |
       key_scan()

              |
              |
        LVGL indev

              |
              |
        页面控件
```

---

# 6. 推荐不要在 LVGL 回调里面读GPIO

不要这样：

```c
按钮事件
{
    gpio_get_level();
}
```

因为：

LVGL任务：

```
lv_timer_handler()
```

频率可能：

```
5~20ms
```

按键会抖动。

---

推荐：

单独任务：

```c
void key_task(void *arg)
{

    while(1)
    {

        scan_key();


        vTaskDelay(
            pdMS_TO_TICKS(10)
        );

    }

}
```

扫描：

```
100Hz
```

足够。

---

# 7. 热成像仪实际UI设计

比如：

```
+----------------+
|                |
|   热成像图      |
|                |
| MAX 36.5℃      |
|                |
+----------------+

 MENU
 ↑ ↓
 OK
```

按键：

| 按键 | LVGL         |
| -- | ------------ |
| 上  | LV_KEY_UP    |
| 下  | LV_KEY_DOWN  |
| 确认 | LV_KEY_ENTER |
| 返回 | LV_KEY_ESC   |

---

# 8. 如果不用LVGL控件，只控制自己的热成像画面？

比如：

按键：

```
+
-
调节温度范围
```

其实更简单：

GPIO扫描：

```c
if(key==KEY_UP)
{
    temp_max+=1;
}


if(key==KEY_DOWN)
{
    temp_max-=1;
}
```

然后：

```c
thermal_update();
```

刷新canvas。

---

对于你的项目：

**ESP32-S3 + ST7789 + MLX90640热成像**

我建议：

* 热成像画面：`lv_canvas`
* 参数菜单：LVGL button / list
* 实体按键：`LV_INDEV_TYPE_KEYPAD`
* FreeRTOS单独key_task扫描

这是工业仪器最常见架构。你后面做成手持热像仪，这套结构基本就是标准方案。
