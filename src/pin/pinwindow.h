#pragma once

#include <QImage>
#include <QTimer>
#include <QWidget>

// 关闭时的状态，用于“恢复关闭的贴图”
struct PinSnapshot {
    QImage source;
    int rotation = 0;
    bool flipH = false;
    bool flipV = false;
    qreal scale = 1.0;
    qreal opacity = 1.0;
    QPoint imagePos;
};

// 贴图窗口：无边框置顶，带阴影；可拖动、滚轮缩放、Ctrl+滚轮调透明度、旋转和翻转、缩略图、鼠标穿透
class PinWindow : public QWidget {
    Q_OBJECT
public:
    explicit PinWindow(const QImage& image);

    QImage image() const { return m_view; } // 当前看到的图（含旋转、翻转）
    void setImage(const QImage& image);     // 编辑完成后替换内容，缩放、旋转复位
    // 窗口四周留有阴影边距，定位以图片左上角为准
    QPoint imagePos() const;
    void moveImageTo(const QPoint& globalTopLeft);

    // 鼠标穿透：点击直接落到下面的窗口，贴图只当参考图；穿透后只能从托盘菜单取消
    void setClickThrough(bool on);
    // 切换鼠标穿透的快捷键（显示在右键菜单里），由设置决定
    static void setClickThroughShortcut(const QString& keys) { s_clickThroughKeys = keys; }
    bool clickThrough() const;
    PinSnapshot snapshot() const;
    void applySnapshot(const PinSnapshot& s);

signals:
    void closeAllRequested();
    void editRequested();
    void ocrRequested();
    void closing(const PinSnapshot& state);
    void clickThroughChanged(bool on);

protected:
    void closeEvent(QCloseEvent* e) override;

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
    inline static QString s_clickThroughKeys;
    void setScale(qreal scale, const QPoint& anchorGlobal);
    void toggleThumbnail();
    void showHint(const QString& text);
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
    bool m_thumbnail = false;
    qreal m_scaleBeforeThumbnail = 1.0;
    qreal m_opacityBeforeClickThrough = 1.0;
    QString m_hint;
    QTimer m_hintTimer;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
