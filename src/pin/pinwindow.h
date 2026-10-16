#pragma once

#include <QImage>
#include <QWidget>

// 贴图窗口：无边框置顶，带阴影；可拖动、滚轮缩放、Ctrl+滚轮调透明度、旋转和翻转
class PinWindow : public QWidget {
    Q_OBJECT
public:
    explicit PinWindow(const QImage& image);

    QImage image() const { return m_view; } // 当前看到的图（含旋转、翻转）
    void setImage(const QImage& image);     // 编辑完成后替换内容，缩放、旋转复位
    // 窗口四周留有阴影边距，定位以图片左上角为准
    QPoint imagePos() const;
    void moveImageTo(const QPoint& globalTopLeft);

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
    void rotate(int quarterTurns);
    void flip(bool horizontal);
    void updateView(); // 按旋转、翻转生成显示用的图，保持图片中心不动
    QRect imageRect() const;
    void copyToClipboard();
    void save();

    QImage m_source;   // 原图
    QImage m_view;     // 旋转、翻转后的图
    int m_rotation = 0; // 顺时针 90° 的次数
    bool m_flipH = false;
    bool m_flipV = false;
    qreal m_scale = 1.0;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
