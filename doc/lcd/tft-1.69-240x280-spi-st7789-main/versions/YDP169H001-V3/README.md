<p align="left"><img alt="OSPTEK" src="./images/logo.png" width="200" /></p>

<h1 align="center">OSPTEK 1.69″ TFT 240×280（ST7789V3 · SPI）</h1>

<p align="center"><b>TFT 屏幕 · SPI · ST7789V3</b></p>

<p align="center"><a href="./README_EN.md">English</a> | 简体中文 · <a href="../../README.md">规格族索引</a></p>

<p align="center">
  <img alt="Size: 1.69 inch" src="https://img.shields.io/badge/Size-1.69%22-3498DB?style=flat-square" />
  <img alt="Resolution: 240x280" src="https://img.shields.io/badge/Resolution-240%C3%97280-8E44AD?style=flat-square" />
  <img alt="Interface: SPI" src="https://img.shields.io/badge/Interface-SPI-27AE60?style=flat-square" />
  <img alt="Driver: ST7789V3" src="https://img.shields.io/badge/Driver-ST7789V3-E7352C?style=flat-square" />
</p>

<p align="center"><img alt="OSPTEK 1.69 寸 240×280 TFT 屏幕（ST7789V3）宣传图" src="./images/product.png" width="640" /></p>

## 目录

- [产品简介](#产品简介)
- [规格参数](#规格参数)
- [示例工程](#示例工程)
- [仓库结构](#仓库结构)
- [相关资料](#相关资料)
- [购买链接](#购买链接)
- [技术支持](#技术支持)

---

## 产品简介

OSPTEK **1.69 寸 240×280 TFT** 屏幕为 **SPI** 彩色面板，显示驱动为 **ST7789V3**。本料号为**单屏**规格。

规格标识（仓库名）：`tft-1.69-240x280-spi-st7789`

当前屏幕版本：**YDP169H001-V3**。电气与外形细节以 [`docs/YDP169H001-V3.pdf`](./docs/YDP169H001-V3.pdf) 为准。

## 规格参数

| 项目 | 规格 |
| ---- | ---- |
| 尺寸 | 1.69 英寸 |
| 类型 | TFT / IPS（彩色） |
| 分辨率 | 240×280 |
| 接口 | SPI（4-wire） |
| 驱动 IC | ST7789V3 |

> 完整外形尺寸、FPC 定义、供电与时序以产品规格书 / 驱动手册为准。

## 示例工程

| 说明 | 路径 |
| ---- | ---- |
| ESP32-S3 · ST7789V3 SPI + LVGL9 | [`examples/esp32s3-tft-1.69-240x280-spi-st7789-bringup/`](./examples/esp32s3-tft-1.69-240x280-spi-st7789-bringup/) |

## 仓库结构

```text
tft-1.69-240x280-spi-st7789/                                # 仓库根（导航见 ../../README.md）
└── versions/
    └── YDP169H001-V3/                                # 本料号完整资料
        ├── README.md
        ├── README_EN.md
        ├── images/
        ├── docs/
        └── examples/
```

## 相关资料

### 本产品资料

| 资料 | 链接 |
| ---- | ---- |
| 屏幕规格书（YDP169H001-V3） | [`docs/YDP169H001-V3.pdf`](./docs/YDP169H001-V3.pdf) |
| 驱动 IC 数据手册（ST7789V3） | [`docs/ST_7789_V3_SPEC_Preliminary_V0_0_200102_8f4b7f4d5d.pdf`](./docs/ST_7789_V3_SPEC_Preliminary_V0_0_200102_8f4b7f4d5d.pdf) |
| 初始化序列（INI） | [`docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.INI`](./docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.INI) |

### 示例工程

- [ESP32-S3 ST7789V3 SPI + LVGL9](./examples/esp32s3-tft-1.69-240x280-spi-st7789-bringup/)

## 购买链接

<p align="center">
  <a href="https://shop110742373.taobao.com/"><img alt="淘宝官方店铺" src="https://img.shields.io/badge/淘宝-官方店铺-FF6A00?style=for-the-badge" /></a>
  &nbsp;&nbsp;
  <a href="https://www.aliexpress.com/store/1105701619"><img alt="速卖通官方店铺" src="https://img.shields.io/badge/速卖通-官方店铺-E62E04?style=for-the-badge&logo=aliexpress&logoColor=white" /></a>
</p>

**国内（淘宝）**

- 店铺：[鱼鹰光电工厂店](https://shop110742373.taobao.com/)

**海外（AliExpress）**

- 店铺：[OSPTEK Official Store](https://www.aliexpress.com/store/1105701619)

## 技术支持

- 技术支持 / 产品咨询：<luyu@osptek.com>
- QQ 技术交流群：**985881096**
- 公司官网：<https://osptek.com/>
- 有任何问题，都可以在本仓库 Issues 中提问

---

<p align="center"><sub>© 2026 OSPTEK 鱼鹰光电 · 本仓库资料采用 CC BY 4.0 许可</sub></p>
