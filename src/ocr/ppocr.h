#pragma once

#include "ocrtext.h"

#include <QImage>
#include <memory>

// PP-OCRv5（mobile）文字检测 + 识别，用 ONNX Runtime 在 CPU 上推理，三个平台结果一致。
// 模型编译进程序资源（:/ocr/），不依赖系统的 OCR 或语言包。不是线程安全的，由 TextRecognizer 串行调用
class PpOcrEngine {
public:
    PpOcrEngine();
    ~PpOcrEngine();

    bool load(QString* error); // 加载模型，约几百毫秒
    QList<OcrFragment> recognize(const QImage& image); // 出错时抛出 std::exception

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
