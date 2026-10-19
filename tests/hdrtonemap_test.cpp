#include "platform/hdrtonemap.h"

#include <QFloat16>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int failures = 0;

static uint16_t half(float f)
{
    const qfloat16 h(f);
    uint16_t b;
    std::memcpy(&b, &h, sizeof(b));
    return b;
}

static void expectNear(const char* name, int got, int want, int tolerance = 1)
{
    if (std::abs(got - want) > tolerance) {
        std::printf("FAIL %s: got %d want %d\n", name, got, want);
        ++failures;
    } else {
        std::printf("ok   %s\n", name);
    }
}

int main()
{
    // “SDR 内容亮度”200 尼特：SDR 内容被系统提亮 2.5 倍
    const std::vector<uint8_t> lut = makeScRgbToSrgbLut(2.5f);
    expectNear("sdr white -> 255", lut[half(2.5f)], 255, 0);
    expectNear("black -> 0", lut[half(0.0f)], 0, 0);
    expectNear("sdr mid gray (srgb 128)", lut[half(0.2158605f * 2.5f)], 128);
    expectNear("hdr highlight clipped", lut[half(10.0f)], 255, 0);
    expectNear("negative (out of gamut) -> 0", lut[half(-0.5f)], 0, 0);

    // 没有提亮（80 尼特）时就是普通的线性 → sRGB
    const std::vector<uint8_t> plain = makeScRgbToSrgbLut(1.0f);
    expectNear("no scaling: mid gray", plain[half(0.2158605f)], 128);
    expectNear("no scaling: white", plain[half(1.0f)], 255, 0);

    // 单调：越亮的输入不会得到更暗的输出
    int prev = 0;
    bool monotonic = true;
    for (float v = 0.0f; v <= 4.0f; v += 0.01f) {
        const int out = lut[half(v)];
        monotonic = monotonic && out >= prev;
        prev = out;
    }
    expectNear("monotonic", monotonic ? 1 : 0, 1, 0);

    // 整张图：R16G16B16A16 一个像素 (2.5, 0, 0.54) → (255, 0, 128)
    const uint16_t px[4] = {half(2.5f), half(0.0f), half(0.2158605f * 2.5f), half(1.0f)};
    const QImage img = scRgbToImage(reinterpret_cast<const uchar*>(px), 1, 1, sizeof(px), lut);
    const QRgb c = img.pixel(0, 0);
    expectNear("image red", qRed(c), 255, 0);
    expectNear("image green", qGreen(c), 0, 0);
    expectNear("image blue", qBlue(c), 128);
    return failures ? 1 : 0;
}
