#pragma once

#include <QColor>
#include <QFrame>

// 预设颜色面板：截图遮罩的子控件（不是独立的弹出窗口，避免在 macOS 上被高层级的遮罩挡住）
class ColorPalette : public QFrame {
    Q_OBJECT
public:
    explicit ColorPalette(QWidget* parent);

    void setCurrent(const QColor& color);
    // 在 anchor（父控件坐标）下方弹出，放不下时放在上方
    void popup(const QRect& anchor);

signals:
    void colorPicked(const QColor& color);
    void moreRequested(); // “更多颜色…”：打开系统取色对话框

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void rebuild();

    QColor m_current;
};
