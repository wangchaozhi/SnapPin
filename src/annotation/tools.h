#pragma once

#include "shapes.h"

#include <QPointF>
#include <memory>

class AnnotationDoc;
class QWidget;

enum class ToolType { None, Rect, Ellipse, Line, Arrow, Pen, Marker, Mosaic, Eraser, Text, Number };

struct ToolContext {
    AnnotationDoc* doc = nullptr;
    QWidget* host = nullptr; // 文字工具在其上放置输入框
    Style style;
    Qt::KeyboardModifiers modifiers; // 当前按下的修饰键（Shift 约束角度 / 比例）
};

// 标注工具（策略模式）：每个工具自己解释鼠标事件，完成时把图形提交给文档
class ITool {
public:
    virtual ~ITool() = default;
    virtual void press(const QPointF& pos, ToolContext& ctx) = 0;
    virtual void move(const QPointF& pos, ToolContext& ctx) = 0;
    virtual void release(const QPointF& pos, ToolContext& ctx) = 0;
    virtual void paintPreview(QPainter&, const RenderContext&) const {}
    // 切换工具或完成截图前调用：提交未完成的内容（如正在输入的文字）
    virtual void finish(ToolContext&) {}
};

std::unique_ptr<ITool> createTool(ToolType type);
