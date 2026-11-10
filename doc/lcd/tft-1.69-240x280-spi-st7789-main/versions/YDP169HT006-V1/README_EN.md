<p align="left"><img alt="OSPTEK" src="./images/logo.png" width="200" /></p>

<h1 align="center">OSPTEK 1.69″ TFT 240×280 (ST7789 · SPI)</h1>

<p align="center"><b>TFT module · SPI · ST7789 · capacitive touch</b></p>

<p align="center"><a href="./README.md">简体中文</a> | English · <a href="../../README_EN.md">Family index</a></p>

<p align="center">
  <img alt="Size: 1.69 inch" src="https://img.shields.io/badge/Size-1.69%22-3498DB?style=flat-square" />
  <img alt="Resolution: 240x280" src="https://img.shields.io/badge/Resolution-240%C3%97280-8E44AD?style=flat-square" />
  <img alt="Interface: SPI" src="https://img.shields.io/badge/Interface-SPI-27AE60?style=flat-square" />
  <img alt="Driver: ST7789" src="https://img.shields.io/badge/Driver-ST7789-E7352C?style=flat-square" />
</p>

<p align="center"><img alt="OSPTEK 1.69 inch 240×280 TFT SPI module (ST7789) product image" src="./images/product.png" width="640" /></p>

## Contents

- [Overview](#overview)
- [Specifications](#specifications)
- [Sample projects](#sample-projects)
- [Repository layout](#repository-layout)
- [Resources](#resources)
- [Buy](#buy)
- [Support](#support)

---

## Overview

OSPTEK **1.69″ 240×280 TFT** is a **SPI** color display module driven by **ST7789**, with capacitive touch (**CST816**). Suited to handheld devices, wearables, and compact portrait HMI.

Spec ID (repository name): `tft-1.69-240x280-spi-st7789`

Current module version: **YDP169HT006-V1**. Electrical and mechanical details follow [`docs/YDP169HT006-V1.pdf`](./docs/YDP169HT006-V1.pdf).

## Specifications

| Item | Spec |
| ---- | ---- |
| Size | 1.69 inch |
| Type | TFT / IPS (color) |
| Resolution | 240×280 |
| Interface | SPI (4-wire) |
| Driver IC | ST7789 |
| Touch driver | CST816 |

> Full outline, FPC definition, power, and timing follow the product datasheet / driver IC datasheet.

## Sample projects

| Description | Path |
| ---- | ---- |
| ESP32-S3 · ST7789 SPI + CST816 + LVGL9 | [`examples/s3-idf5_st7789-spi_cst816-i2c_lvgl-v9/`](./examples/s3-idf5_st7789-spi_cst816-i2c_lvgl-v9/) |
| ESP32-S3 · ST7789 SPI display test | [`examples/display-touch-test/s3-idf_st7789-spi_lvgl-v9/`](./examples/display-touch-test/s3-idf_st7789-spi_lvgl-v9/) |
| ESP32-S3 · CST816 touch I2C test | [`examples/display-touch-test/S3-IDF_CST816-I2C/`](./examples/display-touch-test/S3-IDF_CST816-I2C/) |

## Repository layout

```text
tft-1.69-240x280-spi-st7789/                                # repo root (nav: ../../README_EN.md)
└── versions/
    └── YDP169HT006-V1/                                # full materials for this part number
        ├── README.md
        ├── README_EN.md
        ├── images/
        ├── docs/
        └── examples/
```

## Resources

### Product files

| Resource | Link |
| ---- | ---- |
| Product datasheet (YDP169HT006-V1) | [`docs/YDP169HT006-V1.pdf`](./docs/YDP169HT006-V1.pdf) |
| Mechanical drawing (DWG) | [`docs/YDP_169_HT_006_V1_636100704c.dwg`](./docs/YDP_169_HT_006_V1_636100704c.dwg) |
| Driver IC datasheet (ST7789P3) | [`docs/ST_7789_P3_SPEC_V0_0_Preliminary_f8548bfb64.pdf`](./docs/ST_7789_P3_SPEC_V0_0_Preliminary_f8548bfb64.pdf) |
| Touch driver datasheet (CST816) | [`docs/CST_816_T_v1_3_b7bcb3b8f4.pdf`](./docs/CST_816_T_v1_3_b7bcb3b8f4.pdf) |
| Init sequence (INI) | [`docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.INI`](./docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.INI) |
| Init sequence (text) | [`docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.txt`](./docs/HSD1.69IPS-ST7789V2-2.5Gamma-20210824.txt) |

### Samples

- [ESP32-S3 ST7789 SPI + CST816 + LVGL9](./examples/s3-idf5_st7789-spi_cst816-i2c_lvgl-v9/)
- [ESP32-S3 ST7789 SPI display test](./examples/display-touch-test/s3-idf_st7789-spi_lvgl-v9/)
- [ESP32-S3 CST816 touch I2C test](./examples/display-touch-test/S3-IDF_CST816-I2C/)

## Buy

<p align="center">
  <a href="https://www.aliexpress.com/store/1105701619"><img alt="AliExpress store" src="https://img.shields.io/badge/AliExpress-Official_Store-FF6A00?style=for-the-badge" /></a>
  &nbsp;&nbsp;
  <a href="https://shop110742373.taobao.com/"><img alt="Taobao store" src="https://img.shields.io/badge/Taobao-Official_Store-FF6A00?style=for-the-badge" /></a>
</p>

**Overseas (AliExpress)**

- Store: [OSPTEK Official Store](https://www.aliexpress.com/store/1105701619)

**China (Taobao)**

- Store: [鱼鹰光电工厂店](https://shop110742373.taobao.com/)

## Support

- Technical support / product inquiry: <luyu@osptek.com>
- QQ group (China): **985881096**
- Website: <https://osptek.com/>
- Feel free to open an Issue in this repository if you have any questions

---

<p align="center"><sub>© 2026 OSPTEK · Materials in this repository are licensed under CC BY 4.0</sub></p>
