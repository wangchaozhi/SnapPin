#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>

class PinWindow;

// 中介者：统一管理所有贴图窗口
class PinManager : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~PinManager() override;

    PinWindow* pin(const QImage& image, const QPoint& globalTopLeft);
    bool pinClipboard(); // 剪贴板里没有图片或文字时返回 false
    void toggleVisible();
    void closeAll();
    int count() const { return int(m_pins.size()); }

signals:
    void editRequested(PinWindow* pin);

private:
    QList<QPointer<PinWindow>> m_pins;
    bool m_hidden = false;
};
