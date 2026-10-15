#pragma once

#include <QImage>
#include <QWidget>

// 贴图窗口：无边框置顶，可拖动、滚轮缩放、Ctrl+滚轮调透明度
class PinWindow : public QWidget {
    Q_OBJECT
public:
    explicit PinWindow(const QImage& image);

    QImage image() const { return m_image; }
    void setImage(const QImage& image); // 编辑完成后替换内容，缩放复位为 100%

signals:
    void closeAllRequested();
    void editRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;

private:
    void setScale(qreal scale, const QPoint& anchorGlobal);
    void copyToClipboard();
    void save();

    QImage m_image;
    qreal m_scale = 1.0;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
