<p align="left"><img alt="OSPTEK" src="./images/logo.png" width="200" /></p>

<h1 align="center">OSPTEK 1.69″ TFT 240×280（ST7789 · SPI）</h1>

<p align="center"><b>TFT 模组 · SPI · ST7789 · 电容触摸</b></p>

<p align="center"><a href="./README_EN.md">English</a> | 简体中文 · <a href="../../README.md">规格族索引</a></p>

<p align="center">
  <img alt="Size: 1.69 inch" src="https://img.shields.io/badge/Size-1.69%22-3498DB?style=flat-square" />
  <img alt="Resolution: 240x280" src="https://img.shields.io/badge/Resolution-240%C3%97280-8E44AD?style=flat-square" />
  <img alt="Interface: SPI" src="https://img.shields.io/badge/Interface-SPI-27AE60?style=flat-square" />
  <img alt="Driver: ST7789" src="https://img.shields.io/badge/Driver-ST7789-E7352C?style=flat-square" />
</p>

<p align="center"><img alt="OSPTEK 1.69 寸 240×280 TFT SPI 模组（ST7789）宣传图" src="./images/product.png" width="640" /></p>

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

OSPTEK **1.69 寸 240×280 TFT** 是一款 **SPI** 接口彩色显示模组，显示驱动为 **ST7789**，触摸驱动为 **CST816**。适合手持终端、穿戴与小型竖屏 HMI 等场景。

规格标识（仓库名）：`tft-1.69-240x280-spi-st7789`

当前模组版本：**YDP169HT006-V1**。电气与外形细节以 [`docs/YDP169HT006-V1.pdf`](./docs/YDP169HT006-V1.pdf) 为准。

## 规格参数

| 项目 | 规格 |
| ---- | ---- |
| 尺寸 | 1.69 英寸 |
| 类型 | TFT / IPS（彩色） |
| 分辨率 | 240×280 |
| 接口 | SPI（4-wire） |
| 驱动 IC | ST7789 |
| 触摸驱动 | CST816 |

> 完整外形尺寸、FPC 定义、供电与时序以产品规格书 / 驱动手册为准。

## 示例工程

| 说明 | 路径 |
| ---- | ---- |
| ESP32-S3 · ST7789 SPI + CST816 + LVGL9 | [`examples/s3-idf5_st7789-spi_cst816-i2c_lvgl-v9/`](./examples/s3-idf5_st7789-spi_cst816-i2c_lvgl-v9/) |
| ESP32-S3 · ST7789 SPI 显示测试 | [`examples/display-touch-test/s3-idf_st7789-spi_lvgl-v9/`](./examples/display-touch-test/s3-idf_st7789-spi_lvgl-v9/) |
| ESP32-S3 · CST816 触摸 I2C 测试 | [`examples/display-touch-test/S3-IDF_CST816-I2C/`](./examples/display-touch-test/S3-IDF_CST816-I2C/) |

## 仓库结构

```text
tft-1.69-240x280-spi-st7789/                                # 仓库根（导航见 ../../README.md）
└── versions/
    └── YDP169HT006-V1/                                # 本料号完整资料
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
| 产品规格书（YDP169HT006-V1） | [`docs/YDP169HT006-V1.pdf`](./docs/YDP169HT006-V1.pdf) |
| 外形图（DWG） | [`docs/YDP_169_HT_006_V1_636100704c.dwg`](./docs/YDP_169_HT_006_V1_636100704c.dwg) |
| 驱动 IC 数据手册（ST7789P3） | [`docs/ST_7789_P3_SPEC_V0_0_Preliminary_f8548bfb64.pdf`](./docs/ST_7789_P3_SPEC_V0_0_Preliminary_f8548bfb64.pdf) |
| 触摸驱动手册（CST816） | [`docs/CST_816_T_v1_3_b7bcb3b8f4.pdf`](./docs/CST_816_T_v1_3_b7bcb3b8f4.pdf) |
| 初始化序列（INI） | [`docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.INI`](./docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.INI) |
| 初始化序列（文本） | [`docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.txt`](./docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.txt) |

### 示例工程

- [ESP32-S3 ST7789 SPI + CST816 + LVGL9](./examples/s3-idf5_st7789-spi_cst816-i2c_lvgl-v9/)
- [ESP32-S3 ST7789 SPI 显示测试](./examples/display-touch-test/s3-idf_st7789-spi_lvgl-v9/)
- [ESP32-S3 CST816 触摸 I2C 测试](./examples/display-touch-test/S3-IDF_CST816-I2C/)

## 购买链接

<p align="center">
  <a href="https://shop110742373.taobao.com/"><img alt="淘宝官方店铺" src="https://img.shields.io/badge/淘宝-官方店铺-FF6A00?style=for-the-badge" /></a>
  &nbsp;&nbsp;
  <a href="https://www.aliexpress.com/store/1105701619"><img alt="速卖通官方店铺" src="https://img.shields.io/badge/速卖通-官方店铺-FF6A00?style=for-the-badge" /></a>
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
