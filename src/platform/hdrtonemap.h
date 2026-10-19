#pragma once

#include <QImage>
#include <cstdint>
#include <vector>

// HDR 桌面（Windows 开启 HDR 后，桌面按 scRGB 合成：线性、半精度浮点、1.0 = 80 尼特）转成普通的 sRGB 8 位图片。
// 系统把 SDR 内容（窗口、网页）提亮到“SDR 内容亮度”再合成，这里先除掉这个倍数，SDR 白色还原成 255，
// 比它更亮的 HDR 高光截断成白色，最后做 sRGB 伽马编码。

// 半精度浮点的 16 位编码 → 8 位 sRGB 的查找表（65536 项）。
// sdrWhiteScale：SDR 白色相对 80 尼特的倍数（“SDR 内容亮度”设置，200 尼特就是 2.5）
std::vector<uint8_t> makeScRgbToSrgbLut(float sdrWhiteScale);

// R16G16B16A16_FLOAT 的像素数据 → RGB32 图片
QImage scRgbToImage(const uchar* data, int width, int height, int bytesPerLine, const std::vector<uint8_t>& lut);
