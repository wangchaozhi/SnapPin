#pragma once

#include "annotation/annotationdoc.h"
#include "annotation/tools.h"
#include "platform/platform.h"

#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QWidget>
#include <memory>

class EditorToolbar;
class ImageRegionDetector;
class OverlayState;

enum class FinishAction { Copy, Save, QuickSave, Pin, LongCapture };

// 单块屏幕上的截图遮罩。交互逻辑委托给 OverlayState（状态模式）。
// 多屏时开始框选的那块屏幕的遮罩是“主遮罩”，持有选区和标注，坐标可以超出本屏延伸到其他屏幕；
// 其他屏幕的遮罩变成它的“镜像”：画出选区落在本屏的部分，并把鼠标键盘输入转给主遮罩
class OverlayWindow : public QWidget {
    Q_OBJECT
public:
    OverlayWindow(QScreen* screen, const QPixmap& shot, QList<DetectedWindow> windows);
    ~OverlayWindow() override;

    // ---- 供各状态调用 ----
    void setState(std::unique_ptr<OverlayState> state);
    QRect windowAt(const QPoint& pos) const;
    // 边缘吸附：value 是选区某条边的坐标，离附近窗口 / 控件边缘足够近时返回吸附后的坐标。
    // farEdge 表示右 / 下边；near 用来确定光标所在的窗口（只吸附它内部的控件）
    int snap(int value, Qt::Orientation o, bool farEdge, const QPoint& near) const;
    void setHover(const QRect& r);
    // 鼠标移动时识别到的新框：光标离开当前框时立即换；还在当前框里时（在区域和里面的内容块之间、
    // 跨过空白），新框要稳定一小段时间才换，避免框选来回跳
    void proposeHover(const QRect& r, const QPoint& cursor);
    QRect hover() const { return m_hover; }
    // 同一次截图的所有遮罩（包括自己），用于跨屏选区
    void setPeers(const QList<OverlayWindow*>& peers) { m_peers = peers; }
    // 选区能到达的范围：所有屏幕合起来（本屏局部逻辑坐标）
    QRect bounds() const;
    QRect selection() const { return m_selection; }
    bool hasSelection() const { return !m_selection.isEmpty(); }
    void setSelection(const QRect& r);
    void clearSelection();
    void presetSelection(const QRect& r); // 直接进入调整状态（编辑贴图时使用）
    QScreen* targetScreen() const { return m_screen; }
    void setToolbarVisible(bool visible);
    ToolType toolType() const { return m_toolType; }
    ITool* tool() const { return m_tool.get(); }
    ToolContext& toolContext() { return m_toolCtx; }
    const RenderContext& renderContext() const { return m_renderCtx; }

    void selectTool(ToolType type);
    void finish(FinishAction action);
    void cancel();

signals:
    void finished(const QImage& image, const QRect& globalRect, FinishAction action);
    void cancelled();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void showEvent(QShowEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QImage renderResult() const;
    QPoint offsetTo(const OverlayWindow* other) const; // 本窗口局部坐标加上它就是 other 的局部坐标
    void becomeLead();
    void releaseLead();
    bool forwardToLead(QMouseEvent* e); // 镜像遮罩把鼠标事件转给主遮罩
    void paintMirror(QPainter& p);
    bool handleWheel(const QPoint& pos, int deltaY);
    void refreshHover();
    void paintMagnifier(QPainter& p) const;
    void paintSizeLabel(QPainter& p) const;
    void paintWidthHint(QPainter& p) const;
    void stepRegionHistory(int direction);
    void applySelection(const QRect& r);
    void updateToolbarGeometry();
    QColor colorAtCursor() const;
    QPoint physicalCursor() const;

    QScreen* m_screen;
    QPixmap m_shot;      // 原始截图（逻辑坐标绘制）
    QImage m_shotImage;  // 同一份截图，供放大镜和取色按物理像素读取
    QList<DetectedWindow> m_windows;
    QList<OverlayWindow*> m_peers;
    OverlayWindow* m_lead = nullptr; // 非空：本遮罩是它的镜像
    QPixmap m_composite;             // 主遮罩拼起来的多屏截图（跨屏选区的标注取样、出图用）
    mutable std::unique_ptr<ImageRegionDetector> m_imageRegions; // 按截图像素识别框，第一次悬停时才建

    QRect m_hover;
    QRect m_pendingHover;
    QTimer m_hoverTimer;
    QRect m_selection;
    QPoint m_cursor;
    bool m_cursorInside = false;

    std::unique_ptr<OverlayState> m_state;
    std::unique_ptr<OverlayState> m_retiredState; // 状态切换发生在旧状态的方法内，延后销毁

    AnnotationDoc m_doc;
    ToolType m_toolType = ToolType::None;
    std::unique_ptr<ITool> m_tool;
    ToolContext m_toolCtx;
    RenderContext m_renderCtx;
    EditorToolbar* m_toolbar;

    QTimer m_widthHintTimer; // 运行期间在光标处显示粗细预览
    int m_wheelAccum = 0;
    int m_regionIndex = -1;  // 区域历史中当前显示的位置，-1 表示还没切换过
};
