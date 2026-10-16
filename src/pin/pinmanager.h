#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>

#include "pinwindow.h"

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

    bool restoreClosed(); // 恢复最近关闭的一张贴图（位置、缩放、旋转都还原）；没有可恢复的返回 false
    int clearClickThrough(); // 取消所有贴图的鼠标穿透，返回取消了几张

signals:
    void editRequested(PinWindow* pin);

private:
    PinWindow* create(const QImage& image);

    QList<QPointer<PinWindow>> m_pins;
    QList<PinSnapshot> m_closed; // 最近关闭的在后
    bool m_hidden = false;
};
