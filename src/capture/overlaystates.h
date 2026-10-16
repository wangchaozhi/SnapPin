#pragma once

#include <QPoint>
#include <QPointF>
#include <QRect>

class OverlayWindow;
class QMouseEvent;
class QPainter;

// 遮罩交互状态：空闲(识别窗口) → 拖选 → 调整选区 ⇄ 标注
class OverlayState {
public:
    explicit OverlayState(OverlayWindow* w) : w(w) {}
    virtual ~OverlayState() = default;

    virtual void mousePress(QMouseEvent*) {}
    virtual void mouseMove(QMouseEvent*) {}
    virtual void mouseRelease(QMouseEvent*) {}
    virtual void mouseDoubleClick(QMouseEvent*) {}
    virtual void paint(QPainter&) const {}
    virtual bool showMagnifier() const { return false; }
    virtual bool canNudgeSelection() const { return false; }

protected:
    OverlayWindow* w;
};

class IdleState : public OverlayState {
public:
    explicit IdleState(OverlayWindow* w);
    void mousePress(QMouseEvent* e) override;
    void mouseMove(QMouseEvent* e) override;
    void mouseRelease(QMouseEvent* e) override;
    void paint(QPainter& p) const override;
    bool showMagnifier() const override { return true; }

private:
    bool m_pressed = false;
    QPoint m_pressPos;
};

class SelectingState : public OverlayState {
public:
    SelectingState(OverlayWindow* w, const QPoint& anchor);
    void mousePress(QMouseEvent* e) override;
    void mouseMove(QMouseEvent* e) override;
    void mouseRelease(QMouseEvent* e) override;
    void paint(QPainter& p) const override;
    bool showMagnifier() const override { return true; }

private:
    QPoint m_anchor;
};

class AdjustingState : public OverlayState {
public:
    explicit AdjustingState(OverlayWindow* w);
    void mousePress(QMouseEvent* e) override;
    void mouseMove(QMouseEvent* e) override;
    void mouseRelease(QMouseEvent* e) override;
    void mouseDoubleClick(QMouseEvent* e) override;
    void paint(QPainter& p) const override;
    bool canNudgeSelection() const override { return true; }

private:
    // 0 无；1~8 八个手柄（左上起顺时针）；9 选区内部（移动）；10 拖动选中的标注
    static constexpr int kDragShape = 10;
    int hitTest(const QPoint& pos) const;
    void updateCursor(int hit);

    int m_drag = 0;
    QPointF m_shapeMoved; // 本次拖动标注已累计的位移
    QPoint m_pressPos;
    QRect m_origin;
};

class AnnotatingState : public OverlayState {
public:
    explicit AnnotatingState(OverlayWindow* w);
    void mousePress(QMouseEvent* e) override;
    void mouseMove(QMouseEvent* e) override;
    void mouseRelease(QMouseEvent* e) override;
    void paint(QPainter& p) const override;

private:
    bool m_drawing = false;
};

// 选区边框（含可选手柄），各状态共用
void paintSelectionFrame(QPainter& p, const QRect& r, bool handles);
