#pragma once

#include "ocrtext.h"

#include <QImage>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <memory>

class PpOcrEngine;

struct OcrResult {
    QString text;  // 识别出的文字，按行用 \n 分隔
    QString error; // 识别失败的原因，为空表示成功
    QList<OcrFragment> fragments; // 每段文字和它在图里的位置（像素坐标），翻译后贴回原处用
};

// 文字识别（OCR）：在后台线程里调用 PP-OCRv5，结果在主线程回调；context 销毁后不再回调。
// 模型第一次使用时加载，闲置几分钟后释放内存
class TextRecognizer : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const OcrResult&)>;

    explicit TextRecognizer(QObject* parent = nullptr);
    ~TextRecognizer() override;

    void recognize(const QImage& image, QObject* context, Callback onDone);

private:
    QThreadPool m_pool; // 只有一个线程：模型只加载一份，请求排队执行
    std::unique_ptr<PpOcrEngine> m_engine; // 只在 m_pool 的线程里访问
    QTimer m_idle;
};
