#include "textrecognizer.h"

#include "ppocr.h"

#include <QPointer>

namespace {
constexpr int kUnloadAfterMs = 3 * 60 * 1000;
}

TextRecognizer::TextRecognizer(QObject* parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_idle.setSingleShot(true);
    m_idle.setInterval(kUnloadAfterMs);
    // 模型加载后常驻约 100MB 内存，托盘程序长期运行，闲置时释放
    connect(&m_idle, &QTimer::timeout, this, [this] { m_pool.start([this] { m_engine.reset(); }); });
}

TextRecognizer::~TextRecognizer()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void TextRecognizer::recognize(const QImage& image, QObject* context, Callback onDone)
{
    m_idle.stop();
    QPointer<QObject> guard(context);
    m_pool.start([this, image, guard, onDone] {
        OcrResult result;
        try {
            if (!m_engine) {
                auto engine = std::make_unique<PpOcrEngine>();
                QString error;
                if (engine->load(&error))
                    m_engine = std::move(engine);
                else
                    result.error = tr("无法加载文字识别模型：%1").arg(error);
            }
            if (m_engine)
                result.text = assembleOcrText(m_engine->recognize(image));
        } catch (const std::exception& e) {
            result.error = tr("文字识别失败：%1").arg(QString::fromUtf8(e.what()));
        }
        QMetaObject::invokeMethod(this, [this, guard, onDone, result] {
            m_idle.start();
            if (guard)
                onDone(result);
        }, Qt::QueuedConnection);
    });
}
