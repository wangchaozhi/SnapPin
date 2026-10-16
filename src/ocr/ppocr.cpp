#include "ppocr.h"

#include <QFile>
#include <QThread>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

// 参数与 PaddleOCR / RapidOCR 的默认配置一致
constexpr float kDetThresh = 0.3f;    // 概率图二值化阈值
constexpr float kBoxThresh = 0.5f;    // 文字框平均概率低于它丢弃
constexpr float kUnclipRatio = 1.6f;  // 检测框外扩比例（模型输出的区域比文字小）
constexpr int kDetMinSide = 736;      // 短边放大到这个尺寸再检测，截图里的小字也能检出
constexpr int kDetMaxSide = 4096;      // 限制内存占用（Retina 全屏截图 3840 宽，不缩小）
constexpr int kMaxBoxes = 1000;
constexpr int kRecHeight = 48;
constexpr int kRecMinWidth = 320;
constexpr int kRecMaxWidth = 3200;
constexpr int kRecBatch = 6;
constexpr float kTextScore = 0.5f;    // 识别置信度低于它丢弃

QByteArray readResource(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int roundTo32(double v)
{
    return std::max(32, int(std::lround(v / 32.0)) * 32);
}

// 双线性缩放，直接写进模型输入（NCHW，通道按 B、G、R 排列，与 PaddleOCR 用 OpenCV 读图一致）。
// 采样方式与训练时的 cv2.resize(INTER_LINEAR) 相同：识别模型对缩放算法很敏感，用 Qt 的平滑缩放会丢字。
// src 是 RGB888，缩放到 w×h 写到 dst 的左上角（dst 每行 stride 个元素，每个通道 plane 个元素）
template <typename Normalize>
void resizeInto(const QImage& src, int w, int h, float* dst, int stride, size_t plane, Normalize normalize)
{
    const QRect srcRect = src.rect();
    const double sx = double(srcRect.width()) / w;
    const double sy = double(srcRect.height()) / h;
    std::vector<int> x0(w), x1(w);
    std::vector<float> fx(w);
    for (int x = 0; x < w; ++x) {
        const double fxs = std::clamp((x + 0.5) * sx - 0.5, 0.0, double(srcRect.width() - 1));
        x0[x] = int(fxs);
        x1[x] = std::min(x0[x] + 1, srcRect.width() - 1);
        fx[x] = float(fxs - x0[x]);
    }
    for (int y = 0; y < h; ++y) {
        const double fys = std::clamp((y + 0.5) * sy - 0.5, 0.0, double(srcRect.height() - 1));
        const int y0 = int(fys);
        const int y1 = std::min(y0 + 1, srcRect.height() - 1);
        const float fy = float(fys - y0);
        const uchar* r0 = src.constScanLine(srcRect.y() + y0) + 3 * srcRect.x();
        const uchar* r1 = src.constScanLine(srcRect.y() + y1) + 3 * srcRect.x();
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                const int k = 2 - c; // RGB → BGR
                const float top = r0[3 * x0[x] + k] + (r0[3 * x1[x] + k] - r0[3 * x0[x] + k]) * fx[x];
                const float bottom = r1[3 * x0[x] + k] + (r1[3 * x1[x] + k] - r1[3 * x0[x] + k]) * fx[x];
                dst[c * plane + size_t(y) * stride + x] = normalize(c, top + (bottom - top) * fy);
            }
        }
    }
}

// 只有一个输入、一个输出的模型
std::vector<float> runModel(Ort::Session& session, std::vector<float>& input, const std::vector<int64_t>& shape,
                            std::vector<int64_t>* outShape)
{
    Ort::AllocatorWithDefaultOptions alloc;
    const Ort::AllocatedStringPtr inName = session.GetInputNameAllocated(0, alloc);
    const Ort::AllocatedStringPtr outName = session.GetOutputNameAllocated(0, alloc);
    const char* inNames[] = {inName.get()};
    const char* outNames[] = {outName.get()};
    const Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
    std::vector<Ort::Value> out = session.Run(Ort::RunOptions{nullptr}, inNames, &tensor, 1, outNames, 1);
    *outShape = out[0].GetTensorTypeAndShapeInfo().GetShape();
    const size_t n = out[0].GetTensorTypeAndShapeInfo().GetElementCount();
    const float* p = out[0].GetTensorData<float>();
    return std::vector<float>(p, p + n);
}

} // namespace

struct PpOcrEngine::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "SnapPin"};
    std::unique_ptr<Ort::Session> det;
    std::unique_ptr<Ort::Session> rec;
    QStringList dict; // CTC 输出下标 → 字符；0 为空白

    QList<QRect> detect(const QImage& rgb);
    QList<OcrFragment> recognizeBoxes(const QImage& rgb, const QList<QRect>& boxes);
};

PpOcrEngine::PpOcrEngine() = default;
PpOcrEngine::~PpOcrEngine() = default;

bool PpOcrEngine::load(QString* error)
{
    try {
        auto impl = std::make_unique<Impl>();
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::clamp(QThread::idealThreadCount() / 2, 1, 4));
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const QByteArray detModel = readResource(QStringLiteral(":/ocr/det.onnx"));
        const QByteArray recModel = readResource(QStringLiteral(":/ocr/rec.onnx"));
        if (detModel.isEmpty() || recModel.isEmpty()) {
            *error = QStringLiteral("找不到文字识别模型");
            return false;
        }
        impl->det = std::make_unique<Ort::Session>(impl->env, detModel.constData(), size_t(detModel.size()), options);
        impl->rec = std::make_unique<Ort::Session>(impl->env, recModel.constData(), size_t(recModel.size()), options);

        // 字典存在识别模型的元数据里，每行一个字符；末尾再补一个空格（PaddleOCR 的 use_space_char）
        Ort::AllocatorWithDefaultOptions alloc;
        const Ort::ModelMetadata meta = impl->rec->GetModelMetadata();
        const Ort::AllocatedStringPtr chars = meta.LookupCustomMetadataMapAllocated("character", alloc);
        if (!chars) {
            *error = QStringLiteral("文字识别模型缺少字典");
            return false;
        }
        impl->dict << QString();
        impl->dict << QString::fromUtf8(chars.get()).split(QLatin1Char('\n'));
        impl->dict << QStringLiteral(" ");
        d = std::move(impl);
        return true;
    } catch (const std::exception& e) {
        *error = QString::fromUtf8(e.what());
        return false;
    }
}

QList<OcrFragment> PpOcrEngine::recognize(const QImage& image)
{
    if (!d || image.isNull())
        return {};
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    return d->recognizeBoxes(rgb, d->detect(rgb));
}

// DB 检测：概率图二值化 → 连通区域 → 外接矩形按比例外扩。截图里的文字基本是水平的，不需要旋转框
QList<QRect> PpOcrEngine::Impl::detect(const QImage& rgb)
{
    const int srcW = rgb.width();
    const int srcH = rgb.height();
    double ratio = std::min(srcW, srcH) < kDetMinSide ? double(kDetMinSide) / std::min(srcW, srcH) : 1.0;
    if (std::max(srcW, srcH) * ratio > kDetMaxSide)
        ratio = double(kDetMaxSide) / std::max(srcW, srcH);
    const int w = roundTo32(srcW * ratio);
    const int h = roundTo32(srcH * ratio);
    // 归一化到 [-1, 1]（RapidOCR 对 PP-OCRv5 的配置；用 ImageNet 均值方差时检测框偏大，识别会丢字）
    const size_t plane = size_t(w) * h;
    std::vector<float> input(plane * 3);
    resizeInto(rgb, w, h, input.data(), w, plane, [](int, float v) { return v / 127.5f - 1.0f; });
    std::vector<int64_t> outShape;
    const std::vector<float> prob = runModel(*det, input, {1, 3, h, w}, &outShape);
    if (outShape.size() != 4 || outShape[2] != h || outShape[3] != w)
        return {};

    // 二值化后用 2×2 核膨胀，把笔画间的细缝连起来
    std::vector<uint8_t> mask(plane, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool on = false;
            for (int dy = 0; dy < 2 && !on; ++dy)
                for (int dx = 0; dx < 2 && !on; ++dx)
                    on = y + dy < h && x + dx < w && prob[size_t(y + dy) * w + x + dx] > kDetThresh;
            mask[size_t(y) * w + x] = on;
        }
    }

    QList<QRect> boxes;
    std::vector<int> stack;
    for (size_t start = 0; start < plane && boxes.size() < kMaxBoxes; ++start) {
        if (mask[start] != 1)
            continue;
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        mask[start] = 2;
        stack.assign(1, int(start));
        while (!stack.empty()) { // 8 连通的区域
            const int i = stack.back();
            stack.pop_back();
            const int x = i % w, y = i / w;
            x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    const size_t j = size_t(ny) * w + nx;
                    if (mask[j] == 1) {
                        mask[j] = 2;
                        stack.push_back(int(j));
                    }
                }
            }
        }
        const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
        if (std::min(bw, bh) < 3)
            continue;
        double sum = 0;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                sum += prob[size_t(y) * w + x];
        if (sum / (double(bw) * bh) < kBoxThresh)
            continue;
        // 外扩距离 = 面积 × 比例 / 周长
        const double grow = bw * bh * kUnclipRatio / (2.0 * (bw + bh));
        const QRectF r(x0 - grow, y0 - grow, bw + 2 * grow, bh + 2 * grow);
        if (std::min(r.width(), r.height()) < 5)
            continue;
        const double sx = double(srcW) / w, sy = double(srcH) / h;
        const QRect mapped = QRectF(r.x() * sx, r.y() * sy, r.width() * sx, r.height() * sy).toAlignedRect()
                             & QRect(0, 0, srcW, srcH);
        if (!mapped.isEmpty())
            boxes << mapped;
    }
    return boxes;
}

// CRNN 识别：每个框缩放到 48 像素高，按宽高比排序后分批推理，CTC 贪心解码
QList<OcrFragment> PpOcrEngine::Impl::recognizeBoxes(const QImage& rgb, const QList<QRect>& boxes)
{
    QList<QImage> crops;
    for (const QRect& box : boxes) {
        QImage crop = rgb.copy(box).convertToFormat(QImage::Format_RGB888);
        if (crop.height() >= crop.width() * 1.5) // 竖排文字转成横排再识别
            crop = crop.transformed(QTransform().rotate(-90));
        crops << crop;
    }
    std::vector<int> order(crops.size());
    std::iota(order.begin(), order.end(), 0);
    auto aspect = [&](int i) { return double(crops[i].width()) / std::max(1, crops[i].height()); };
    std::sort(order.begin(), order.end(), [&](int a, int b) { return aspect(a) < aspect(b); });

    QList<OcrFragment> fragments;
    for (size_t begin = 0; begin < order.size(); begin += kRecBatch) {
        const size_t end = std::min(order.size(), begin + kRecBatch);
        const int n = int(end - begin);
        double maxAspect = double(kRecMinWidth) / kRecHeight;
        for (size_t k = begin; k < end; ++k)
            maxAspect = std::max(maxAspect, aspect(order[k]));
        const int width = std::min(kRecMaxWidth, int(std::ceil(kRecHeight * maxAspect)));
        const size_t plane = size_t(kRecHeight) * width;
        std::vector<float> input(plane * 3 * n, 0.0f); // 右侧补 0（归一化后的灰色）
        for (int b = 0; b < n; ++b) {
            const QImage& crop = crops[order[begin + b]];
            const int rw = std::clamp(int(std::ceil(kRecHeight * aspect(order[begin + b]))), 1, width);
            resizeInto(crop, rw, kRecHeight, input.data() + plane * 3 * b, width, plane,
                       [](int, float v) { return v / 127.5f - 1.0f; });
        }
        std::vector<int64_t> shape;
        const std::vector<float> out = runModel(*rec, input, {n, 3, kRecHeight, width}, &shape);
        if (shape.size() != 3 || shape[0] != n)
            continue;
        const int steps = int(shape[1]);
        const int classes = int(shape[2]);
        for (int b = 0; b < n; ++b) {
            QString text;
            double conf = 0;
            int count = 0;
            int prev = 0;
            for (int t = 0; t < steps; ++t) {
                const float* p = out.data() + (size_t(b) * steps + t) * classes;
                const int best = int(std::max_element(p, p + classes) - p);
                if (best != 0 && best != prev && best < dict.size()) {
                    text += dict[best];
                    conf += p[best];
                    ++count;
                }
                prev = best;
            }
            if (count == 0 || conf / count < kTextScore || text.trimmed().isEmpty())
                continue;
            fragments.append({QRectF(boxes[order[begin + b]]), text});
        }
    }
    return fragments;
}
