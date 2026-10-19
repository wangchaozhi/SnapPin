#include "hdrtonemap.h"

#include <QFloat16>
#include <cmath>
#include <cstring>

std::vector<uint8_t> makeScRgbToSrgbLut(float sdrWhiteScale)
{
    const float scale = sdrWhiteScale > 0 ? sdrWhiteScale : 1.0f;
    std::vector<uint8_t> lut(65536);
    for (uint32_t bits = 0; bits < 65536; ++bits) {
        const uint16_t b = uint16_t(bits);
        qfloat16 h;
        std::memcpy(&h, &b, sizeof(b));
        float v = float(h) / scale;
        if (!(v > 0.0f)) // 负数、NaN（scRGB 允许色域外的负值）
            v = 0.0f;
        if (v > 1.0f) // 比 SDR 白色更亮的 HDR 高光
            v = 1.0f;
        const float srgb = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        lut[bits] = uint8_t(std::lround(srgb * 255.0f));
    }
    return lut;
}

QImage scRgbToImage(const uchar* data, int width, int height, int bytesPerLine, const std::vector<uint8_t>& lut)
{
    QImage img(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        const auto* src = reinterpret_cast<const uint16_t*>(data + size_t(y) * size_t(bytesPerLine));
        auto* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < width; ++x, src += 4)
            dst[x] = qRgb(lut[src[0]], lut[src[1]], lut[src[2]]);
    }
    return img;
}
